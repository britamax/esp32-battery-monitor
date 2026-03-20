#pragma once

// ============================================================
// BattAnalyzer — ButtonHandler.h
// Multi-fungsi tombol GPIO3
// 1x klik: next page OLED
// 2x klik: silence alarm (snooze 10 menit)
// 3x klik: toggle OLED on/off
// Hold 3s: toggle relay manual (relay 1)
// Boot hold 5s: reset password
// Boot hold 10s: reset WiFi
// ============================================================

#include <Arduino.h>
#include "Config.h"
#include "OLEDDisplay.h"
#include "BuzzerManager.h"
#include "NVSManager.h"

#define BTN_DEBOUNCE    50
#define BTN_DBLWIN      400    // window multi-click
#define BTN_HOLD_RELAY  3000
#define BTN_SNOOZE_MS   600000UL

class ButtonHandler {
public:
    bool alarmSnoozed  = false;
    bool calibRequest  = false;
    bool wifiResetReq  = false;

    unsigned long snoozeUntil = 0;

    void begin() {
        pinMode(PIN_BUTTON, INPUT_PULLUP);
    }

    void handle() {
        _updateSnooze();
        _readButton();
    }

    bool isAlarmSnoozed() {
        return alarmSnoozed && millis() < snoozeUntil;
    }

    // Cek boot hold — panggil dari setup() sebelum task dimulai
    void checkBootHold() {
        if (digitalRead(PIN_BUTTON) != LOW) return;
        Serial.println("[BTN] Boot hold terdeteksi...");
        oled.showStatus("Tahan 5s=Pwd", "Tahan 10s=WiFi", "Lepas=Batal");

        unsigned long start = millis();
        unsigned long held  = 0;
        bool reached5 = false;

        while (digitalRead(PIN_BUTTON) == LOW) {
            held = millis() - start;
            if (!reached5 && held >= 5000) {
                reached5 = true;
                oled.showStatus("Lepas=Reset Pwd", "Tahan=Reset WiFi", "");
                buzzer.play(BUZZ_SHORT);
            }
            if (held >= 10000) {
                oled.showStatus("!! RESET WiFi !!", "Menghapus...", "Restart...");
                buzzer.play(BUZZ_SIRENE);
                delay(2000);
                nvs.clearWifiOnly();
                ESP.restart();
            }
            delay(50);
        }

        if (held >= 5000) {
            oled.showStatus("Reset Password", "admin / 123123", "Restart...");
            nvs.resetAuth();
            buzzer.play(BUZZ_SHORT); delay(200); buzzer.play(BUZZ_SHORT);
            delay(2000);
            ESP.restart();
        }
        // held < 500 atau < 5000 → batal
        oled.showStatus("Dibatalkan", "", "");
        delay(1000);
    }

private:
    bool          _lastRaw    = HIGH;
    bool          _state      = HIGH;
    unsigned long _debounceT  = 0;
    unsigned long _pressTime  = 0;
    unsigned long _releaseT   = 0;
    int           _clicks     = 0;
    bool          _holdDone   = false;
    bool          _inHold     = false;

    void _updateSnooze() {
        if (alarmSnoozed && millis() >= snoozeUntil)
            alarmSnoozed = false;
    }

    void _readButton() {
        bool raw = digitalRead(PIN_BUTTON);
        unsigned long now = millis();

        if (raw != _lastRaw) { _debounceT = now; _lastRaw = raw; }
        if (now - _debounceT < BTN_DEBOUNCE) return;

        bool pressed = (raw == LOW);

        // Tombol baru ditekan
        if (pressed && _state == HIGH) {
            _state = LOW; _pressTime = now;
            _holdDone = false; _inHold = false;
        }

        // Tombol masih ditekan — cek hold
        if (pressed && _state == LOW && !_holdDone) {
            unsigned long held = now - _pressTime;
            if (held >= BTN_HOLD_RELAY && !_inHold) {
                _inHold = true;
                oled.showStatus("Toggle Relay 1", "Tahan...", "");
            }
        }

        // Tombol dilepas
        if (!pressed && _state == LOW) {
            _state = HIGH;
            unsigned long held = now - _pressTime;

            if (_holdDone) { _holdDone = false; _inHold = false; return; }

            if (_inHold && held >= BTN_HOLD_RELAY) {
                _inHold = false; _clicks = 0;
                _doRelayToggle();
                return;
            }

            _inHold = false;
            _clicks++;
            _releaseT = now;
        }

        // Window multi-click habis → proses
        if (_clicks > 0 && !pressed && (now - _releaseT >= BTN_DBLWIN)) {
            _process(_clicks);
            _clicks = 0;
        }
    }

    void _process(int n) {
        switch (n) {
            case 1: _do1Click(); break;
            case 2: _do2Click(); break;
            case 3: _do3Click(); break;
        }
    }

    void _do1Click() {
        oled.nextPage();
        Serial.println("[BTN] Next OLED page");
    }

    void _do2Click() {
        buzzer.stop();
        alarmSnoozed = true;
        snoozeUntil  = millis() + BTN_SNOOZE_MS;
        oled.showStatus("Alarm Snoozed", "10 menit", "");
        buzzer.play(BUZZ_SHORT);
        delay(1200);
        Serial.println("[BTN] Alarm snoozed 10min");
    }

    void _do3Click() {
        bool nowOn = !nvs.getOledOn();
        nvs.setOledOn(nowOn);
        oled.setEnabled(nowOn);
        buzzer.play(BUZZ_SHORT);
        if (!nowOn) { buzzer.play(BUZZ_SHORT); delay(150); buzzer.play(BUZZ_SHORT); }
        Serial.printf("[BTN] OLED %s\n", nowOn ? "ON" : "OFF");
    }

    void _doRelayToggle() {
        Serial.println("[BTN] Toggle Relay 1 (manual)");
        // Signal ke main loop — relay handling ada di RelayManager
        // Gunakan flag global
        extern bool relayManualToggle;
        relayManualToggle = true;
        buzzer.play(BUZZ_SHORT);
    }
};

ButtonHandler btnHandler;
bool relayManualToggle = false;
