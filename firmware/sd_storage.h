#pragma once

#include <stddef.h>
#include <stdint.h>

bool sd_storage_init();
bool sd_storage_is_ready();
bool sd_storage_start_session();
void sd_storage_stop_session();
bool sd_storage_is_recording();
bool sd_storage_save_jpeg(const uint8_t* data, size_t len, char* out_path, size_t out_path_len);
bool sd_storage_append_video_frame(const uint8_t* data, size_t len, uint32_t width, uint32_t height);
void sd_storage_set_avi_frame_interval_ms(uint32_t interval_ms);
uint32_t sd_storage_get_avi_frame_interval_ms();
uint32_t sd_storage_session_count_cached();
unsigned long sd_storage_saved_count();
const char* sd_storage_session_dir();
const char* sd_storage_video_path();
unsigned long sd_storage_video_frame_count();
