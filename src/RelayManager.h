#pragma once

// ============================================================
// BattAnalyzer — RelayManager.h
// Modul relay driver transistor S8550 (PNP)
//
// MASALAH: ESP32-C3 output HIGH = 3.3V, VCC modul = 5V
// Selisih 1.7V menyebabkan transistor S8550 tetap "bocor"
// sehingga relay tidak mau mati meski pin HIGH.
//
// SOLUSI: High-Z (High Impedance) trick
//   ON  → pinMode OUTPUT + digitalWrite LOW  → transistor ON  → relay ON
//   OFF → pinMode INPUT (floating)           → base transistor mengambang
//          → tidak ada arus basis → transistor OFF total → relay OFF
//
// Mapping pin:
//   Relay 1 (GPIO0)  = beban (PERHATIAN: strapping pin, sesaat aktif saat boot)
//   Relay 2 (GPIO4)  = cutoff baterai otomatis
//   Relay 3 (GPIO5)  = beban jadwal
//   Relay 4 (GPIO6)  = beban jadwal
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
            // High-Z: set INPUT agar pin mengambang → transistor OFF → relay OFF
            pinMode(pins[i], INPUT);
            state[i] = false;
        }
        Serial.println("[RELAY] 4 relay init OFF (High-Z mode, S8550 driver)");
        Serial.println("[RELAY] R1=beban R2=cutoff R3=beban R4=beban");
    }

    void set(int idx, bool on) {
        if (idx < 0 || idx >= RELAY_COUNT) return;
        state[idx] = on;
        if (on) {
            // Aktifkan: OUTPUT + LOW → transistor ON → relay ON
            pinMode(pins[idx], OUTPUT);
            digitalWrite(pins[idx], LOW);
        } else {
            // Matikan: INPUT (High-Z) → base mengambang → transistor OFF → relay OFF
            pinMode(pins[idx], INPUT);
        }
        Serial.printf("[RELAY] R%d = %s (GPIO%d = %s)\n",
            idx + 1, on ? "ON" : "OFF",
            pins[idx], on ? "OUTPUT+LOW" : "INPUT(High-Z)");
    }

    void toggle(int idx) {
        if (idx < 0 || idx >= RELAY_COUNT) return;
        set(idx, !state[idx]);
    }

    // Cutoff baterai — Relay 2 (IDX_RELAY_CUTOFF = 1)
    void setCutoff(bool disconnect) {
        set(IDX_RELAY_CUTOFF, disconnect);
        Serial.printf("[RELAY] Cutoff: %s\n",
            disconnect ? "PUTUS (R2 ON)" : "HUBUNG (R2 OFF)");
    }

    bool getCutoffState() {
        return state[IDX_RELAY_CUTOFF];
    }

    // Cek jadwal relay 1,3,4 (semua kecuali cutoff R2)
    void handleSchedule() {
        if (!ntpMgr.synced) return;
        int hour, minute;
        ntpMgr.getHourMin(hour, minute);
        if (hour < 0) return;
        int dayBit = ntpMgr.getDayBit();

        for (int i = 0; i < RELAY_COUNT; i++) {
            if (i == IDX_RELAY_CUTOFF) continue;

            int relayNum = i + 1;
            if (!nvs.getRelayEn(relayNum)) continue;
            if (!(nvs.getRelayDayMask(relayNum) & dayBit)) continue;

            int onh = nvs.getRelayOnH(relayNum), onm = nvs.getRelayOnM(relayNum);
            int ofh = nvs.getRelayOffH(relayNum), ofm = nvs.getRelayOffM(relayNum);

            int nowMin = hour * 60 + minute;
            int onMin  = onh  * 60 + onm;
            int offMin = ofh  * 60 + ofm;

            bool shouldOn;
            if (onMin < offMin)
                shouldOn = (nowMin >= onMin && nowMin < offMin);
            else
                shouldOn = (nowMin >= onMin || nowMin < offMin);

            if (state[i] != shouldOn) set(i, shouldOn);
        }
    }

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