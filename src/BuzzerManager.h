#pragma once

// ============================================================
// BattAnalyzer — BuzzerManager.h
// Passive Buzzer Module KY-012/FC-04 — Active LOW trigger
// Modul punya transistor onboard: I/O=LOW → buzzer bunyi
//
// Fix dari versi sebelumnya:
//   ledcWriteTone() → output HIGH rata-rata → buzzer DIAM
//   ledcWrite(ch,0) → output LOW  → buzzer BUNYI
//   Jadi semua logika ON/OFF dibalik via _buzzOn() / _buzzOff()
// ============================================================

#include <Arduino.h>
#include "Config.h"
#include "NVSManager.h"

enum BuzzPattern {
    BUZZ_NONE      = 0,
    BUZZ_SHORT     = 1,   // 1x 80ms — konfirmasi
    BUZZ_SIRENE    = 2,   // 3x 200ms — peringatan
    BUZZ_SOS       = 3,   // ... --- ... morse SOS
    BUZZ_QUAKE     = 4,   // beep cepat 100ms — gempa
    BUZZ_BATT_LOW  = 5,   // beep panjang 500ms — V rendah
    BUZZ_OVERCURR  = 6,   // beep sedang 300ms — I berlebih
    BUZZ_DOUBLE    = 7,   // 2x beep cepat
    BUZZ_TRIPLE    = 8    // 3x beep cepat
};

class BuzzerManager {
public:
    bool isEnabled = true;
    bool isActive  = false;

    void begin() {
        ledcSetup(BUZZER_CHANNEL, 2000, BUZZER_RESOLUTION);
        ledcAttachPin(PIN_BUZZER, BUZZER_CHANNEL);
        _buzzOff();  // pastikan diam saat boot
        isEnabled = true;
        Serial.println("[BUZZ] OK — test bunyi 3x...");
        // Test singkat saat boot untuk verifikasi buzzer aktif
        delay(200);
        play(BUZZ_SHORT); delay(100);
        play(BUZZ_SHORT); delay(100);
        play(BUZZ_SHORT);
        Serial.println("[BUZZ] Test selesai. Jika tidak bunyi, cek wiring PIN_BUZZER=GPIO1");
    }

    void startAlarm(BuzzPattern p) {
        if (!isEnabled) return;
        _pattern    = p;
        _patternIdx = 0;
        _nextTick   = millis();
        _phase      = true;
        isActive    = true;
    }

    void stop() {
        _buzzOff();
        isActive = false;
        _pattern = BUZZ_NONE;
    }

    void setEnabled(bool v) {
        isEnabled = v;
        if (!v) stop();
    }

    // Blocking — untuk feedback tombol, boot tone
    void play(BuzzPattern p) {
        if (!isEnabled) return;
        switch (p) {
            case BUZZ_SHORT:
                _beep(TONE_OK, 80);
                break;
            case BUZZ_DOUBLE:
                _beep(TONE_OK, 60); delay(80);
                _beep(TONE_OK, 60);
                break;
            case BUZZ_TRIPLE:
                _beep(TONE_OK, 60); delay(60);
                _beep(TONE_OK, 60); delay(60);
                _beep(TONE_OK, 60);
                break;
            case BUZZ_SIRENE:
                _beep(TONE_WARN, 200); delay(100);
                _beep(TONE_WARN, 200); delay(100);
                _beep(TONE_WARN, 200);
                break;
            case BUZZ_SOS:
                _playSOS();
                break;
            default: break;
        }
    }

    // Non-blocking — dipanggil dari loop()
    void handle() {
        if (!isActive || !isEnabled) return;
        unsigned long now = millis();
        if (now < _nextTick) return;

        switch (_pattern) {
            case BUZZ_QUAKE: {
                // 100ms ON / 100ms OFF × 20 = 4 detik total
                if (_patternIdx >= 40) { stop(); return; }
                _phase ? _buzzOn(TONE_ALERT) : _buzzOff();
                _phase = !_phase;
                _patternIdx++;
                _nextTick = now + 100;
                break;
            }
            case BUZZ_BATT_LOW: {
                // 500ms ON / 1500ms OFF × 5
                if (_patternIdx >= 10) { stop(); return; }
                _phase ? _buzzOn(TONE_ALARM) : _buzzOff();
                _phase = !_phase;
                _patternIdx++;
                _nextTick = now + (_phase ? 1500 : 500);
                break;
            }
            case BUZZ_OVERCURR: {
                // 300ms ON / 300ms OFF × 10
                if (_patternIdx >= 20) { stop(); return; }
                _phase ? _buzzOn(TONE_WARN) : _buzzOff();
                _phase = !_phase;
                _patternIdx++;
                _nextTick = now + 300;
                break;
            }
            case BUZZ_SIRENE: {
                // Non-blocking sirene: 200ms ON / 100ms OFF × 3
                if (_patternIdx >= 6) { stop(); return; }
                if (_patternIdx % 2 == 0) _buzzOn(TONE_WARN);
                else _buzzOff();
                _nextTick = now + (_patternIdx % 2 == 0 ? 200 : 100);
                _patternIdx++;
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

    // ── Logika buzzer untuk modul KY-012 / FC-04 ─────────────
    // Modul ini punya transistor NPN onboard:
    //   S/IN = HIGH → transistor ON → buzzer berbunyi
    //   S/IN = LOW  → transistor OFF → buzzer diam
    // ledcWriteTone(ch, freq) → menghasilkan PWM dengan duty ~50%
    //   → rata-rata tegangan ~1.65V (antara HIGH dan LOW)
    //   → transistor aktif sebagian → BUZZER BUNYI
    // ledcWrite(ch, 0) → duty cycle 0% → output selalu LOW
    //   → transistor OFF → BUZZER DIAM
    // ledcWrite(ch, 255) → duty cycle 100% → output selalu HIGH  
    //   → transistor ON penuh → BUZZER BUNYI (DC, tanpa nada)

    void _buzzOn(int freq) {
        // PWM tone → buzzer bunyi dengan frekuensi tertentu
        ledcWriteTone(BUZZER_CHANNEL, freq);
    }

    void _buzzOff() {
        // duty=0 → output LOW → transistor OFF → diam
        ledcWriteTone(BUZZER_CHANNEL, 0);
        ledcWrite(BUZZER_CHANNEL, 0);
    }

    void _beep(int freq, int durMs) {
        _buzzOn(freq);
        delay(durMs);
        _buzzOff();
    }

    void _playSOS() {
        int dot = 100, dash = 300, gap = 100, space = 300;
        for (int i = 0; i < 3; i++) { _beep(TONE_ALERT, dot);  delay(gap); }
        delay(space);
        for (int i = 0; i < 3; i++) { _beep(TONE_ALERT, dash); delay(gap); }
        delay(space);
        for (int i = 0; i < 3; i++) { _beep(TONE_ALERT, dot);  delay(gap); }
    }
};

BuzzerManager buzzer;