#pragma once

// ============================================================
// BattAnalyzer — NTPManager.h
// Sync waktu NTP — boot + tiap 6 jam
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "NVSManager.h"

class NTPManager {
public:
    bool          synced    = false;
    String        lastSync  = "--";
    unsigned long lastSyncMs = 0;

    void begin() {
        if (!nvs.getNtpEn()) return;
        sync();
    }

    void handle() {
        if (!nvs.getNtpEn()) return;
        if (WiFi.status() != WL_CONNECTED) return;
        unsigned long now = millis();
        if (lastSyncMs == 0 || now - lastSyncMs >= NTP_SYNC_INTERVAL)
            sync();
    }

    void sync() {
        if (WiFi.status() != WL_CONNECTED) return;
        int    offset = nvs.getNtpOffset();
        String server = nvs.getNtpServer();
        configTime((long)offset * 3600, 0, server.c_str(),
                   "time.cloudflare.com", "time.google.com");
        struct tm ti;
        int retry = 0;
        while (!getLocalTime(&ti) && retry++ < 10) delay(500);
        if (getLocalTime(&ti)) {
            synced = true;
            lastSyncMs = millis();
            char buf[24];
            strftime(buf, sizeof(buf), "%d %b %Y %H:%M:%S", &ti);
            lastSync = String(buf);
            Serial.printf("[NTP] Sync OK: %s\n", buf);
        } else {
            synced = false;
            Serial.println("[NTP] Sync GAGAL");
        }
    }

    String getTimeStr() {
        if (!synced) return "--:--:--";
        struct tm ti;
        if (!getLocalTime(&ti)) return "--:--:--";
        char buf[24];
        strftime(buf, sizeof(buf), "%d %b %Y %H:%M:%S", &ti);
        return String(buf);
    }

    String getTimeShort() {
        if (!synced) return "--:--:--";
        struct tm ti;
        if (!getLocalTime(&ti)) return "--:--:--";
        char buf[10];
        strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
        return String(buf);
    }

    // Ambil jam & menit untuk relay schedule
    void getHourMin(int& hour, int& minute) {
        hour = -1; minute = 0;
        if (!synced) return;
        struct tm ti;
        if (!getLocalTime(&ti)) return;
        hour   = ti.tm_hour;
        minute = ti.tm_min;
    }

    // Day of week bit: bit0=Sun, bit1=Mon, ... bit6=Sat
    int getDayBit() {
        if (!synced) return 0;
        struct tm ti;
        if (!getLocalTime(&ti)) return 0;
        return (1 << ti.tm_wday);
    }

    void applyTimezone() {
        int offset = nvs.getNtpOffset();
        configTime((long)offset * 3600, 0, nvs.getNtpServer().c_str());
        synced = false;
        sync();
    }
};

NTPManager ntpMgr;
