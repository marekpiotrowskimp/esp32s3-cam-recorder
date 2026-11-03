#pragma once

#include "esp_camera.h"

bool camera_init();
camera_fb_t* camera_snapshot();
void camera_fb_release(camera_fb_t* fb);
bool camera_is_ready();
const char* camera_resolution_str();
bool camera_set_frame_size(framesize_t frame_size);
framesize_t camera_get_frame_size();
void camera_deinit();
int camera_last_error();  // returns esp_err_t from last failed init