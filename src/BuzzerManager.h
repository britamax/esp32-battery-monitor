#pragma once

// ============================================================
// BattAnalyzer — BuzzerManager.h
// Passive Buzzer via LEDC PWM — 6 pola alarm
// ============================================================

#include <Arduino.h>
#include "Config.h"
#include "NVSManager.h"

enum BuzzPattern {
    BUZZ_NONE      = 0,
    BUZZ_SHORT     = 1,   // 1x 80ms — konfirmasi
    BUZZ_SIRENE    = 2,   // 3x 200ms — peringatan
    BUZZ_SOS       = 3,   // --- ... --- morse SOS
    BUZZ_QUAKE     = 4,   // beep cepat 100ms — gempa
    BUZZ_BATT_LOW  = 5,   // beep panjang 500ms — V < cutoff
    BUZZ_OVERCURR  = 6    // beep sedang 300ms — I > max
};

class BuzzerManager {
public:
    bool isEnabled = true;
    bool isActive  = false;

    void begin() {
        ledcSetup(BUZZER_CHANNEL, 1000, BUZZER_RESOLUTION);
        ledcAttachPin(PIN_BUZZER, BUZZER_CHANNEL);
        ledcWrite(BUZZER_CHANNEL, 0);
        isEnabled = nvs.getMqttEn() || true; // selalu on kecuali diset
    }

    // Non-blocking alarm — dijalankan di handle()
    void startAlarm(BuzzPattern p) {
        _pattern    = p;
        _patternIdx = 0;
        _nextTick   = millis();
        _phase      = true;   // mulai dengan ON
        isActive    = true;
    }

    void stop() {
        ledcWrite(BUZZER_CHANNEL, 0);
        isActive   = false;
        _pattern   = BUZZ_NONE;
    }

    void setEnabled(bool v) {
        isEnabled = v;
        if (!v) stop();
    }

    // Blocking — untuk feedback tombol, startup, dll
    void play(BuzzPattern p) {
        if (!isEnabled) return;
        switch (p) {
            case BUZZ_SHORT:
                _beep(TONE_OK, 80); break;
            case BUZZ_SIRENE:
                _beep(TONE_WARN, 200); delay(100);
                _beep(TONE_WARN, 200); delay(100);
                _beep(TONE_WARN, 200); break;
            case BUZZ_SOS: _playSOS(); break;
            default: break;
        }
    }

    // Panggil dari loop() — handle non-blocking alarm
    void handle() {
        if (!isActive || !isEnabled) return;
        unsigned long now = millis();
        if (now < _nextTick) return;

        switch (_pattern) {
            case BUZZ_QUAKE: {
                // Alternating 100ms ON / 100ms OFF, repeat 20x
                if (_patternIdx >= 40) { stop(); return; }
                if (_phase) ledcWriteTone(BUZZER_CHANNEL, TONE_ALERT);
                else        ledcWrite(BUZZER_CHANNEL, 0);
                _phase = !_phase;
                _patternIdx++;
                _nextTick = now + 100;
                break;
            }
            case BUZZ_BATT_LOW: {
                // 500ms ON / 1500ms OFF, repeat 5x
                if (_patternIdx >= 10) { stop(); return; }
                if (_phase) ledcWriteTone(BUZZER_CHANNEL, TONE_ALARM);
                else        ledcWrite(BUZZER_CHANNEL, 0);
                _phase = !_phase;
                _patternIdx++;
                _nextTick = now + (_phase ? 1500 : 500);
                break;
            }
            case BUZZ_OVERCURR: {
                // 300ms ON / 300ms OFF, repeat 10x
                if (_patternIdx >= 20) { stop(); return; }
                if (_phase) ledcWriteTone(BUZZER_CHANNEL, TONE_WARN);
                else        ledcWrite(BUZZER_CHANNEL, 0);
                _phase = !_phase;
                _patternIdx++;
                _nextTick = now + 300;
                break;
            }
            default: stop(); break;
        }
    }

private:
    BuzzPattern   _pattern    = BUZZ_NONE;
    int           _patternIdx = 0;
    unsigned long _nextTick   = 0;
    bool          _phase      = true;

    void _beep(int freq, int durMs) {
        ledcWriteTone(BUZZER_CHANNEL, freq);
        delay(durMs);
        ledcWrite(BUZZER_CHANNEL, 0);
    }

    void _playSOS() {
        // S = ...   O = ---   S = ...
        int dot = 100, dash = 300, gap = 100, space = 300;
        // S
        for (int i = 0; i < 3; i++) { _beep(TONE_ALERT, dot); delay(gap); }
        delay(space);
        // O
        for (int i = 0; i < 3; i++) { _beep(TONE_ALERT, dash); delay(gap); }
        delay(space);
        // S
        for (int i = 0; i < 3; i++) { _beep(TONE_ALERT, dot); delay(gap); }
    }
};

BuzzerManager buzzer;
