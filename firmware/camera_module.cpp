#include "camera_module.h"
#include "config.h"
#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static bool g_camera_ready = false;
static framesize_t g_frame_size = CAM_FRAME_SIZE;
static int g_last_error = 0;
static SemaphoreHandle_t g_camera_mutex = nullptr;

static void camera_mutex_init() {
    if (g_camera_mutex == nullptr) {
        g_camera_mutex = xSemaphoreCreateMutex();
    }
}

static bool camera_mutex_take() {
    if (g_camera_mutex == nullptr) return true;
    return xSemaphoreTake(g_camera_mutex, pdMS_TO_TICKS(300)) == pdTRUE;
}

static void camera_mutex_give() {
    if (g_camera_mutex != nullptr) {
        xSemaphoreGive(g_camera_mutex);
    }
}

void camera_deinit() {
    if (!camera_mutex_take()) {
        DEBUG_PRINTLN("[CAMERA] Deinit: mutex timeout");
        return;
    }
    if (g_camera_ready) {
        esp_camera_deinit();
        g_camera_ready = false;
    }
    camera_mutex_give();
}

bool camera_init() {
    camera_mutex_init();

    camera_config_t config = {};

    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;

    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;

    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;

    config.xclk_freq_hz = CAM_XCLK_FREQ;
    config.frame_size = g_frame_size;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = CAM_JPEG_QUALITY;
    config.fb_count = CAM_FB_COUNT;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        DEBUG_PRINTF("[CAMERA] Init failed: 0x%x\n", err);
        g_camera_ready = false;
        g_last_error = (int)err;
        return false;
    }

    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor != nullptr) {
        sensor->set_brightness(sensor, 0);
        sensor->set_contrast(sensor, 0);
        sensor->set_saturation(sensor, 0);
        sensor->set_sharpness(sensor, 0);
        sensor->set_whitebal(sensor, 1);
        sensor->set_exposure_ctrl(sensor, 1);
        sensor->set_gain_ctrl(sensor, 1);
        sensor->set_hmirror(sensor, 0);
        sensor->set_vflip(sensor, CAM_USE_VFLIP);
    }

    g_camera_ready = true;
    g_last_error = 0;
    DEBUG_PRINTF("[CAMERA] Ready - %s JPEG q%d\n", camera_resolution_str(), CAM_JPEG_QUALITY);
    return true;
}

camera_fb_t* camera_snapshot() {
    if (!camera_mutex_take()) {
        DEBUG_PRINTLN("[CAMERA] Snapshot: mutex timeout");
        return nullptr;
    }
    if (!g_camera_ready) {
        camera_mutex_give();
        return nullptr;
    }
    camera_fb_t* fb = esp_camera_fb_get();
    camera_mutex_give();
    if (fb == nullptr) {
        DEBUG_PRINTLN("[CAMERA] Capture failed");
    }
    return fb;
}

void camera_fb_release(camera_fb_t* fb) {
    if (fb != nullptr) {
        esp_camera_fb_return(fb);
    }
}

bool camera_is_ready() {
    return g_camera_ready;
}

const char* camera_resolution_str() {
    switch (g_frame_size) {
        case FRAMESIZE_QQVGA: return "160x120";
        case FRAMESIZE_QVGA: return "320x240";
        case FRAMESIZE_VGA: return "640x480";
        case FRAMESIZE_SVGA: return "800x600";
        default: return "custom";
    }
}

bool camera_set_frame_size(framesize_t frame_size) {
    if (g_frame_size == frame_size) {
        return true;
    }
    g_frame_size = frame_size;
    // deinit/init bez mutexa — woła camera_deinit() który sam bierze mutex
    // ale tu potrzebujemy trzymać mutex przez CAŁY reinit żeby zapobiec
    // race condition gdzie snapshot wchodzi między deinit a init
    if (!camera_mutex_take()) {
        DEBUG_PRINTLN("[CAMERA] set_frame_size: mutex timeout");
        return false;
    }
    if (g_camera_ready) {
        esp_camera_deinit();
        g_camera_ready = false;
    }
    // init bez mutexa (już trzymamy)
    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = CAM_XCLK_FREQ;
    config.frame_size = g_frame_size;
    config.pixel_format = PIXFORMAT_JPEG;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = CAM_JPEG_QUALITY;
    config.fb_count = CAM_FB_COUNT;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        DEBUG_PRINTF("[CAMERA] Reinit failed: 0x%x\n", err);
        g_last_error = (int)err;
        camera_mutex_give();
        return false;
    }
    sensor_t* sensor = esp_camera_sensor_get();
    if (sensor != nullptr) {
        sensor->set_brightness(sensor, 0);
        sensor->set_contrast(sensor, 0);
        sensor->set_saturation(sensor, 0);
        sensor->set_sharpness(sensor, 0);
        sensor->set_whitebal(sensor, 1);
        sensor->set_exposure_ctrl(sensor, 1);
        sensor->set_gain_ctrl(sensor, 1);
        sensor->set_hmirror(sensor, 0);
        sensor->set_vflip(sensor, CAM_USE_VFLIP);
    }
    g_camera_ready = true;
    g_last_error = 0;
    camera_mutex_give();
    DEBUG_PRINTF("[CAMERA] Frame size changed to %s\n", camera_resolution_str());
    return true;
}

framesize_t camera_get_frame_size() {
    return g_frame_size;
}

int camera_last_error() {
    return g_last_error;
}
