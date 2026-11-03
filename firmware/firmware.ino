#include "camera_module.h"
#include "config.h"
#include "sd_storage.h"
#include "web_server_esp.h"

#include <WiFi.h>
#include <time.h>

static void wifi_connect() {
    DEBUG_PRINTF("[WIFI] Connecting to %s\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(true, true);
    delay(300);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < 20000) {
        delay(500);
        DEBUG_PRINT(".");
    }
    DEBUG_PRINTLN("");

    if (WiFi.status() == WL_CONNECTED) {
        DEBUG_PRINTF("[WIFI] Connected, IP: %s\n", WiFi.localIP().toString().c_str());
        return;
    }

    DEBUG_PRINTLN("[WIFI] Connection failed");
}

static void time_sync() {
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    DEBUG_PRINTLN("[TIME] Syncing NTP...");
    configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);

    struct tm timeinfo = {};
    for (int i = 0; i < 20; ++i) {
        if (getLocalTime(&timeinfo, 500)) {
            DEBUG_PRINTF("[TIME] Synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                         timeinfo.tm_year + 1900,
                         timeinfo.tm_mon + 1,
                         timeinfo.tm_mday,
                         timeinfo.tm_hour,
                         timeinfo.tm_min,
                         timeinfo.tm_sec);
            return;
        }
        delay(250);
    }

    DEBUG_PRINTLN("[TIME] NTP sync unavailable, using uptime fallback");
}

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(1500);

    DEBUG_PRINTLN("");
    DEBUG_PRINTLN("============================");
    DEBUG_PRINTLN("spy-esp MVP boot");
    DEBUG_PRINTLN("XIAO ESP32-S3 Sense");
    DEBUG_PRINTLN("============================");

    wifi_connect();
    time_sync();

    DEBUG_PRINTLN("[BOOT] Starting camera...");
    camera_init();

    DEBUG_PRINTLN("[BOOT] Mounting SD...");
    sd_storage_init();

    if (WiFi.status() == WL_CONNECTED) {
        DEBUG_PRINTLN("[BOOT] Starting web server...");
        web_server_start();
    } else {
        DEBUG_PRINTLN("[BOOT] Skipping web server, Wi-Fi unavailable");
    }
}

void loop() {
    if (WiFi.status() != WL_CONNECTED && web_server_is_running()) {
        web_server_stop();
    } else if (WiFi.status() == WL_CONNECTED && !web_server_is_running()) {
        web_server_start();
    }

    web_server_update();
    delay(10);
}
