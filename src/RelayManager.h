#pragma once

// ============================================================
// BattAnalyzer — RelayManager.h
// 4 relay: jadwal harian + cutoff baterai otomatis
// Relay 1 (GPIO0) = cutoff baterai
// Relay 2-4 (GPIO4,5,6) = beban jadwal
// ============================================================

#include <Arduino.h>
#include "Config.h"
#include "NVSManager.h"
#include "NTPManager.h"

class RelayManager {
public:
    bool state[RELAY_COUNT] = {false, false, false, false};
    const int pins[RELAY_COUNT] = {PIN_RELAY_1, PIN_RELAY_2, PIN_RELAY_3, PIN_RELAY_4};

    void begin() {
        for (int i = 0; i < RELAY_COUNT; i++) {
            pinMode(pins[i], OUTPUT);
            digitalWrite(pins[i], RELAY_OFF);
            state[i] = false;
        }
        Serial.println("[RELAY] 4 relay diinit OFF");
    }

    // Set relay manual (index 0-3)
    void set(int idx, bool on) {
        if (idx < 0 || idx >= RELAY_COUNT) return;
        state[idx] = on;
        digitalWrite(pins[idx], on ? RELAY_ON : RELAY_OFF);
        Serial.printf("[RELAY] R%d = %s\n", idx + 1, on ? "ON" : "OFF");
    }

    void toggle(int idx) {
        if (idx < 0 || idx >= RELAY_COUNT) return;
        set(idx, !state[idx]);
    }

    // Cutoff baterai — Relay 1 (idx 0)
    void setCutoff(bool disconnect) {
        // disconnect=true → relay ON memutus circuit (NO relay)
        set(0, disconnect);
        Serial.printf("[RELAY] Cutoff baterai: %s\n", disconnect ? "PUTUS" : "HUBUNG");
    }

    // Cek jadwal relay 2-4 tiap menit
    void handleSchedule() {
        if (!ntpMgr.synced) return;
        int hour, minute;
        ntpMgr.getHourMin(hour, minute);
        if (hour < 0) return;
        int dayBit = ntpMgr.getDayBit();

        for (int i = 1; i < RELAY_COUNT; i++) {  // relay 2-4 (index 1-3)
            if (!nvs.getRelayEn(i + 1)) continue;
            if (!(nvs.getRelayDayMask(i + 1) & dayBit)) continue;

            int onh = nvs.getRelayOnH(i + 1), onm = nvs.getRelayOnM(i + 1);
            int ofh = nvs.getRelayOffH(i + 1), ofm = nvs.getRelayOffM(i + 1);

            bool shouldOn;
            int nowMin  = hour * 60 + minute;
            int onMin   = onh  * 60 + onm;
            int offMin  = ofh  * 60 + ofm;

            if (onMin < offMin)
                shouldOn = (nowMin >= onMin && nowMin < offMin);
            else
                shouldOn = (nowMin >= onMin || nowMin < offMin);  // melewati tengah malam

            if (state[i] != shouldOn) set(i, shouldOn);
        }
    }

    // JSON status semua relay
    String toJson() {
        String j = "[";
        for (int i = 0; i < RELAY_COUNT; i++) {
            if (i > 0) j += ",";
            j += state[i] ? "true" : "false";
        }
        j += "]";
        return j;
    }
};

RelayManager relayMgr;
