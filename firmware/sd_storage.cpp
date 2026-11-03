#include "sd_storage.h"
#include "config.h"

#include <SD.h>
#include <SPI.h>
#include <sys/time.h>
#include <time.h>

// ---------------------------------------------------------------------------
// MP4 (ISO 14496-12) MJPEG writer
//
// Strategy: write ftyp+moov FIRST, then mdat — no seek-back needed.
// Since moov size depends on frame_count (unknown at start), we record
// frames into a temporary .tmp file during recording.
// On finalize:
//   1. Compute moov size from frame_count
//   2. Write ftyp + moov + mdat_header + copy(.tmp) into final .mp4
//   3. Delete .tmp
//
// This avoids any backward seek which Arduino SD (SPI) does not support.
// All integers are big-endian (MP4 standard).
// ---------------------------------------------------------------------------

static const char TMP_EXT[]   = ".tmp";
static const char IDX_EXT[]   = ".idx";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static bool          g_sd_ready              = false;
static bool          g_recording             = false;
static unsigned long g_saved_count           = 0;
static char          g_session_dir[64]       = {0};
static char          g_session_stamp[32]     = {0};
static char          g_video_path[96]        = {0};  // final .mp4
static char          g_tmp_path[96]          = {0};  // temp .tmp (raw frames)
static char          g_idx_path[96]          = {0};  // index .idx (offset+size per frame)
static File          g_tmp_file;
static File          g_idx_file;
static uint32_t      g_video_frame_count     = 0;
static uint32_t      g_video_width           = 0;
static uint32_t      g_video_height          = 0;
static uint32_t      g_mov_frame_interval_ms = AVI_FRAME_INTERVAL_MS;
static uint32_t      g_session_count_cached  = 0;

// ---------------------------------------------------------------------------
// Time helpers
// ---------------------------------------------------------------------------
static bool get_time_stamp_parts(struct tm* tm_out, uint16_t* millis_out) {
    if (!tm_out) return false;
    struct timeval tv = {};
    gettimeofday(&tv, nullptr);
    localtime_r(&tv.tv_sec, tm_out);
    if (tm_out->tm_year + 1900 < 2024) return false;
    if (millis_out) *millis_out = (uint16_t)(tv.tv_usec / 1000U);
    return true;
}

static void build_session_stamp(char* out, size_t len) {
    struct tm t = {};
    if (get_time_stamp_parts(&t, nullptr))
        snprintf(out, len, "%04d-%02d-%02d_%02d-%02d-%02d",
                 t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    else
        snprintf(out, len, "uptime_%lu", millis());
}

static void build_frame_name(char* out, size_t len, unsigned long n) {
    struct tm t = {}; uint16_t ms = 0;
    if (get_time_stamp_parts(&t, &ms))
        snprintf(out, len, "frame_%04d-%02d-%02d_%02d-%02d-%02d_%03u_%06lu.jpg",
                 t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
                 (unsigned)ms, n);
    else
        snprintf(out, len, "frame_%06lu.jpg", n);
}

static uint32_t mac_time() {
    struct timeval tv = {};
    gettimeofday(&tv, nullptr);
    return (uint32_t)(tv.tv_sec + 2082844800UL);
}

// ---------------------------------------------------------------------------
// Big-endian write helpers (Arduino File)
// ---------------------------------------------------------------------------
static void f_be16(File& f, uint16_t v) {
    uint8_t b[2] = {(uint8_t)(v>>8),(uint8_t)v}; f.write(b,2);
}
static void f_be32(File& f, uint32_t v) {
    uint8_t b[4] = {(uint8_t)(v>>24),(uint8_t)(v>>16),(uint8_t)(v>>8),(uint8_t)v}; f.write(b,4);
}
static void f_cc(File& f, const char* cc) { f.write((const uint8_t*)cc, 4); }
static void f_zeros(File& f, size_t n) {
    uint8_t z = 0; for (size_t i=0;i<n;i++) f.write(&z,1);
}

// Little-endian helpers (index file)
static void f_le32(File& f, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v,(uint8_t)(v>>8),(uint8_t)(v>>16),(uint8_t)(v>>24)}; f.write(b,4);
}
static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

// ---------------------------------------------------------------------------
// moov writer — writes directly to Arduino File (forward-only, no seek)
// mdat_start: absolute byte offset in final .mp4 where JPEG data begins
// idx: open index File, rewound to start
// ---------------------------------------------------------------------------
static void write_moov(File& out, uint32_t frame_count, uint32_t width, uint32_t height,
                       uint32_t time_scale, uint32_t frame_duration,
                       uint32_t mdat_start, File& idx) {

    uint32_t now            = mac_time();
    uint32_t total_dur      = frame_count * frame_duration;

    static const char HDLR_NAME[] = "VideoHandler";
    const uint32_t hdlr_name_len  = sizeof(HDLR_NAME); // 13 incl. null

    const uint32_t hdlr_sz  = 8+4+4+4+12+hdlr_name_len;  // 45
    const uint32_t mdhd_sz  = 32;
    const uint32_t vmhd_sz  = 20;
    const uint32_t dinf_sz  = 36;
    const uint32_t stsd_sz  = 102; // 8+4+4 + (8+6+2+4+12+4+4+4+4+2+32+2+2) = 16+86
    const uint32_t stts_sz  = 24;
    const uint32_t stsc_sz  = 28;
    const uint32_t stsz_sz  = 20 + frame_count*4;
    const uint32_t stco_sz  = 20; // 8+4+4+4 (1 entry)
    const uint32_t stbl_sz  = 8+stsd_sz+stts_sz+stsc_sz+stsz_sz+stco_sz;
    const uint32_t minf_sz  = 8+vmhd_sz+dinf_sz+stbl_sz;
    const uint32_t mdia_sz  = 8+mdhd_sz+hdlr_sz+minf_sz;
    const uint32_t tkhd_sz  = 92;
    const uint32_t trak_sz  = 8+tkhd_sz+mdia_sz;
    const uint32_t mvhd_sz  = 108;
    const uint32_t moov_sz  = 8+mvhd_sz+trak_sz;

    // moov
    f_be32(out, moov_sz); f_cc(out, "moov");

    // mvhd
    f_be32(out,mvhd_sz); f_cc(out,"mvhd"); f_be32(out,0);
    f_be32(out,now); f_be32(out,now); f_be32(out,time_scale); f_be32(out,total_dur);
    f_be32(out,0x00010000); f_be16(out,0x0100); f_zeros(out,10);
    f_be32(out,0x00010000); f_be32(out,0); f_be32(out,0);
    f_be32(out,0); f_be32(out,0x00010000); f_be32(out,0);
    f_be32(out,0); f_be32(out,0); f_be32(out,0x40000000);
    f_zeros(out,24); f_be32(out,2);

    // trak
    f_be32(out,trak_sz); f_cc(out,"trak");

    // tkhd
    f_be32(out,tkhd_sz); f_cc(out,"tkhd"); f_be32(out,0x00000003);
    f_be32(out,now); f_be32(out,now); f_be32(out,1); f_be32(out,0); f_be32(out,total_dur);
    f_zeros(out,8);
    f_be16(out,0); f_be16(out,0); f_be16(out,0); f_be16(out,0);
    f_be32(out,0x00010000); f_be32(out,0); f_be32(out,0);
    f_be32(out,0); f_be32(out,0x00010000); f_be32(out,0);
    f_be32(out,0); f_be32(out,0); f_be32(out,0x40000000);
    f_be32(out,width<<16); f_be32(out,height<<16);

    // mdia
    f_be32(out,mdia_sz); f_cc(out,"mdia");

    // mdhd
    f_be32(out,mdhd_sz); f_cc(out,"mdhd"); f_be32(out,0);
    f_be32(out,now); f_be32(out,now); f_be32(out,time_scale); f_be32(out,total_dur);
    f_be16(out,0); f_be16(out,0);

    // hdlr
    f_be32(out,hdlr_sz); f_cc(out,"hdlr"); f_be32(out,0); f_be32(out,0);
    f_cc(out,"vide"); f_zeros(out,12);
    out.write((const uint8_t*)HDLR_NAME, hdlr_name_len);

    // minf
    f_be32(out,minf_sz); f_cc(out,"minf");

    // vmhd
    f_be32(out,vmhd_sz); f_cc(out,"vmhd"); f_be32(out,0x00000001); f_be16(out,0); f_zeros(out,6);

    // dinf / dref
    f_be32(out,dinf_sz); f_cc(out,"dinf");
    f_be32(out,28); f_cc(out,"dref"); f_be32(out,0); f_be32(out,1);
    f_be32(out,12); f_cc(out,"url "); f_be32(out,0x00000001);

    // stbl
    f_be32(out,stbl_sz); f_cc(out,"stbl");

    // stsd / jpeg VisualSampleEntry
    f_be32(out,stsd_sz); f_cc(out,"stsd"); f_be32(out,0); f_be32(out,1);
    f_be32(out,stsd_sz-16); f_cc(out,"jpeg");
    f_zeros(out,6); f_be16(out,1);          // reserved + data_ref_idx
    f_be16(out,0); f_be16(out,0);           // pre_defined + reserved
    f_zeros(out,12);                         // pre_defined[3]
    f_be16(out,(uint16_t)width); f_be16(out,(uint16_t)height);
    f_be32(out,0x00480000); f_be32(out,0x00480000); f_be32(out,0);
    f_be16(out,1); f_zeros(out,32); f_be16(out,24); f_be16(out,0xFFFF);

    // stts
    f_be32(out,stts_sz); f_cc(out,"stts"); f_be32(out,0); f_be32(out,1);
    f_be32(out,frame_count); f_be32(out,frame_duration);

    // stsc (1 chunk = all frames)
    f_be32(out,stsc_sz); f_cc(out,"stsc"); f_be32(out,0); f_be32(out,1);
    f_be32(out,1); f_be32(out,frame_count); f_be32(out,1);

    // stsz (variable frame sizes from index)
    f_be32(out,stsz_sz); f_cc(out,"stsz"); f_be32(out,0); f_be32(out,0); f_be32(out,frame_count);
    {
        uint8_t buf[8];
        idx.seek(0);
        for (uint32_t i = 0; i < frame_count; i++) {
            uint32_t sz = 0;
            if (idx.read(buf, 8) == 8) sz = le32(buf+4);
            f_be32(out, sz);
        }
    }

    // stco (1 chunk offset = mdat_start)
    f_be32(out,stco_sz); f_cc(out,"stco"); f_be32(out,0); f_be32(out,1); f_be32(out,mdat_start);
}

// ---------------------------------------------------------------------------
// moov size calculator (must match write_moov exactly)
// ---------------------------------------------------------------------------
static uint32_t moov_size(uint32_t frame_count) {
    const uint32_t hdlr_sz  = 8+4+4+4+12+13;
    const uint32_t mdhd_sz  = 32;
    const uint32_t vmhd_sz  = 20;
    const uint32_t dinf_sz  = 36;
    const uint32_t stsd_sz  = 102;
    const uint32_t stts_sz  = 24;
    const uint32_t stsc_sz  = 28;
    const uint32_t stsz_sz  = 20 + frame_count*4;
    const uint32_t stco_sz  = 20;
    const uint32_t stbl_sz  = 8+stsd_sz+stts_sz+stsc_sz+stsz_sz+stco_sz;
    const uint32_t minf_sz  = 8+vmhd_sz+dinf_sz+stbl_sz;
    const uint32_t mdia_sz  = 8+mdhd_sz+hdlr_sz+minf_sz;
    const uint32_t tkhd_sz  = 92;
    const uint32_t trak_sz  = 8+tkhd_sz+mdia_sz;
    const uint32_t mvhd_sz  = 108;
    return 8+mvhd_sz+trak_sz;
}

// ---------------------------------------------------------------------------
// Video file lifecycle
// ---------------------------------------------------------------------------
static bool start_video_file() {
    if (!g_sd_ready || g_session_dir[0] == '\0') return false;

    const char* stamp = g_session_stamp[0] ? g_session_stamp : "session";
    snprintf(g_video_path, sizeof(g_video_path), "%s/recording_%s.mp4", g_session_dir, stamp);
    snprintf(g_tmp_path,   sizeof(g_tmp_path),   "%s/recording_%s.tmp", g_session_dir, stamp);
    snprintf(g_idx_path,   sizeof(g_idx_path),   "%s/recording_%s.idx", g_session_dir, stamp);

    // Remove stale files from previous failed run
    if (SD.exists(g_tmp_path))   SD.remove(g_tmp_path);
    if (SD.exists(g_idx_path))   SD.remove(g_idx_path);
    if (SD.exists(g_video_path)) SD.remove(g_video_path);

    g_tmp_file = SD.open(g_tmp_path, FILE_WRITE);
    g_idx_file = SD.open(g_idx_path, FILE_WRITE);
    if (!g_tmp_file || !g_idx_file) {
        DEBUG_PRINTLN("[SD] Failed to create recording files");
        if (g_tmp_file) g_tmp_file.close();
        if (g_idx_file) g_idx_file.close();
        return false;
    }

    g_video_frame_count = 0;
    g_video_width  = 0;
    g_video_height = 0;

    DEBUG_PRINTF("[SD] Recording started: %s\n", g_video_path);
    return true;
}

static void finalize_video_file() {
    if (!g_tmp_file) return;

    g_tmp_file.flush();
    g_tmp_file.close();
    if (g_idx_file) { g_idx_file.flush(); g_idx_file.close(); }

    if (g_video_frame_count == 0 || g_video_width == 0) {
        DEBUG_PRINTLN("[SD] Finalize: no frames");
        SD.remove(g_tmp_path);
        SD.remove(g_idx_path);
        return;
    }

    // Compute layout:
    //   ftyp  = 24 bytes
    //   moov  = moov_size(frame_count)
    //   mdat header = 8 bytes
    //   mdat data start = 24 + moov_sz + 8
    uint32_t m_sz       = moov_size(g_video_frame_count);
    uint32_t mdat_start = 24 + m_sz + 8;  // absolute offset of first JPEG byte in .mp4

    // Get total raw frame data size
    File tmp_r = SD.open(g_tmp_path, FILE_READ);
    if (!tmp_r) {
        DEBUG_PRINTLN("[SD] Finalize: cannot open .tmp");
        return;
    }
    uint32_t raw_size = (uint32_t)tmp_r.size();
    uint32_t mdat_atom_size = 8 + raw_size;

    File idx_r = SD.open(g_idx_path, FILE_READ);
    if (!idx_r) {
        DEBUG_PRINTLN("[SD] Finalize: cannot open .idx");
        tmp_r.close();
        return;
    }

    // Open final .mp4 (write, forward-only — no seek needed)
    File mp4 = SD.open(g_video_path, FILE_WRITE);
    if (!mp4) {
        DEBUG_PRINTLN("[SD] Finalize: cannot create .mp4");
        tmp_r.close(); idx_r.close();
        return;
    }

    // 1. ftyp (24 bytes)
    f_be32(mp4, 24);     f_cc(mp4, "ftyp");
    f_cc(mp4, "isom");   f_be32(mp4, 0x00000200);
    f_cc(mp4, "isom");   f_cc(mp4, "iso2");

    // 2. moov
    write_moov(mp4, g_video_frame_count, g_video_width, g_video_height,
               1000, g_mov_frame_interval_ms, mdat_start, idx_r);

    // 3. mdat header
    f_be32(mp4, mdat_atom_size);
    f_cc(mp4, "mdat");

    // 4. Copy raw frames from .tmp — large PSRAM buffer, yield to feed WDT
    {
        const size_t BUF_SIZE = 32768;
        uint8_t* buf = (uint8_t*)ps_malloc(BUF_SIZE);
        if (!buf) buf = (uint8_t*)malloc(4096);
        if (!buf) {
            DEBUG_PRINTLN("[SD] Finalize: no memory for copy buffer");
            mp4.close(); tmp_r.close(); idx_r.close();
            return;
        }
        tmp_r.seek(0);
        size_t n;
        while ((n = tmp_r.read(buf, BUF_SIZE)) > 0) {
            mp4.write(buf, n);
            yield();
        }
        free(buf);
    }

    mp4.flush();
    mp4.close();
    tmp_r.close();
    idx_r.close();

    SD.remove(g_tmp_path);
    SD.remove(g_idx_path);

    DEBUG_PRINTF("[SD] MP4 finalized: %s (%lu frames, moov_sz=%lu, mdat_start=%lu)\n",
                 g_video_path, (unsigned long)g_video_frame_count,
                 (unsigned long)m_sz, (unsigned long)mdat_start);
}

// ---------------------------------------------------------------------------
// Misc helpers
// ---------------------------------------------------------------------------
static bool ensure_dir(const char* path) {
    if (SD.exists(path)) return true;
    return SD.mkdir(path);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool sd_storage_init() {
    if (!SD.begin(SD_CS_PIN)) {
        DEBUG_PRINTLN("[SD] Mount failed");
        g_sd_ready = false; return false;
    }
    if (SD.cardType() == CARD_NONE) {
        DEBUG_PRINTLN("[SD] No card");
        g_sd_ready = false; return false;
    }
    if (!ensure_dir(SD_ROOT_DIR)) {
        DEBUG_PRINTLN("[SD] Root dir failed");
        g_sd_ready = false; return false;
    }
    g_sd_ready = true;
    g_session_count_cached = 0;
    File root = SD.open(SD_ROOT_DIR);
    if (root && root.isDirectory()) {
        File e = root.openNextFile();
        while (e) { if (e.isDirectory()) g_session_count_cached++; e.close(); e = root.openNextFile(); }
        root.close();
    }
    DEBUG_PRINTF("[SD] Ready, size=%lluMB\n", SD.cardSize()/(1024ULL*1024ULL));
    return true;
}

bool sd_storage_is_ready() { return g_sd_ready; }

bool sd_storage_start_session() {
    if (!g_sd_ready) return false;
    build_session_stamp(g_session_stamp, sizeof(g_session_stamp));
    snprintf(g_session_dir, sizeof(g_session_dir), "%s/session_%s", SD_ROOT_DIR, g_session_stamp);
    if (!ensure_dir(g_session_dir)) {
        DEBUG_PRINTLN("[SD] Session dir failed");
        g_session_dir[0] = '\0';
        return false;
    }
    g_recording = true;
    g_saved_count = 0;
    g_video_path[0] = '\0';
    g_session_count_cached++;
    DEBUG_PRINTF("[SD] Session: %s\n", g_session_dir);
    return start_video_file();
}

void sd_storage_stop_session() {
    finalize_video_file();
    g_recording = false;
    DEBUG_PRINTLN("[SD] Session stopped");
}

bool sd_storage_is_recording() { return g_recording; }

bool sd_storage_save_jpeg(const uint8_t* data, size_t len, char* out_path, size_t out_path_len) {
    if (!g_sd_ready || !g_recording || !data || !len || !g_session_dir[0]) return false;

    char name[96], path[128];
    build_frame_name(name, sizeof(name), g_saved_count+1);
    snprintf(path, sizeof(path), "%s/%s", g_session_dir, name);

    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    size_t w = f.write(data, len);
    f.close();
    if (w != len) return false;

    g_saved_count++;
    if (out_path && out_path_len > 0) snprintf(out_path, out_path_len, "%s", path);
    return true;
}

bool sd_storage_append_video_frame(const uint8_t* data, size_t len, uint32_t width, uint32_t height) {
    if (!g_sd_ready || !g_recording || !g_tmp_file || !g_idx_file || !data || !len)
        return false;

    if (!g_video_width) { g_video_width = width; g_video_height = height; }

    // offset within .tmp file (= offset relative to mdat_data_start in final .mp4)
    // The index stores this relative offset; we add mdat_start at moov-write time via stco.
    // Actually stco only has 1 entry (mdat_start), so stsz+stco are enough.
    // We only need size in index; store offset too for safety.
    uint32_t frame_offset = (uint32_t)g_tmp_file.position();
    size_t written = g_tmp_file.write(data, len);
    if (written != len) {
        DEBUG_PRINTLN("[SD] Frame write failed");
        return false;
    }

    f_le32(g_idx_file, frame_offset);
    f_le32(g_idx_file, (uint32_t)len);
    g_video_frame_count++;

    DEBUG_PRINTF("[SD] Frame %lu (%u bytes)\n", (unsigned long)g_video_frame_count, (unsigned)len);
    return true;
}

void sd_storage_set_avi_frame_interval_ms(uint32_t ms) {
    if (ms >= 50) g_mov_frame_interval_ms = ms;
}
uint32_t sd_storage_get_avi_frame_interval_ms() { return g_mov_frame_interval_ms; }
uint32_t sd_storage_session_count_cached()       { return g_session_count_cached; }
unsigned long sd_storage_saved_count()           { return g_saved_count; }
const char* sd_storage_session_dir()             { return g_session_dir; }
const char* sd_storage_video_path()              { return g_video_path; }
unsigned long sd_storage_video_frame_count()     { return g_video_frame_count; }
