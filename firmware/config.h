#pragma once

#include <Arduino.h>

// -----------------------------------------------------------------------------
// Wi-Fi
// -----------------------------------------------------------------------------
#include "secrets.h"   // WIFI_SSID, WIFI_PASS — patrz secrets.h.example

// -----------------------------------------------------------------------------
// Time / NTP
// -----------------------------------------------------------------------------
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"
#define TZ_INFO      "CET-1CEST,M3.5.0/2,M10.5.0/3"

// -----------------------------------------------------------------------------
// HTTP
// -----------------------------------------------------------------------------
#define HTTP_SERVER_PORT 80
#define HTTP_STREAM_PORT 81
#define HTTP_MEDIA_PORT  82

// -----------------------------------------------------------------------------
// Kamera OV3660 - XIAO ESP32S3 Sense
// -----------------------------------------------------------------------------
#define CAMERA_MODEL_XIAO_ESP32S3
#define CAM_FRAME_SIZE   FRAMESIZE_QVGA
#define CAM_JPEG_QUALITY 12
#define CAM_FB_COUNT     4
#define CAM_XCLK_FREQ    20000000
#define CAM_USE_VFLIP    1

// -----------------------------------------------------------------------------
// SD Card
// -----------------------------------------------------------------------------
#define SD_CS_PIN               21
#define SD_MOUNT_POINT          "/sdcard"
#define SD_ROOT_DIR             "/captures"
#define AVI_FRAME_INTERVAL_MS   200
#define JPEG_SAVE_INTERVAL_MS   1000

// -----------------------------------------------------------------------------
// Debug
// -----------------------------------------------------------------------------
#define SERIAL_BAUD   115200
#define DEBUG_ENABLED true

#if DEBUG_ENABLED
  #define DEBUG_PRINT(x)    Serial.print(x)
  #define DEBUG_PRINTLN(x)  Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif
