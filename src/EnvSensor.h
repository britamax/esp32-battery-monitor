#pragma once

// ============================================================
// BattAnalyzer — EnvSensor.h
// AHT20 (suhu + kelembaban) + BMP280 (tekanan + ketinggian)
// Prediksi cuaca dari tren tekanan 3 jam
// ============================================================

#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include "Config.h"
#include "NVSManager.h"

// Histori tekanan untuk prediksi cuaca (36 titik × 5 menit = 3 jam)
#define PRESS_HISTORY   36

class EnvSensor {
public:
    float temperature = 0.0f;  // °C dari AHT20
    float humidity    = 0.0f;  // %RH dari AHT20
    float pressure    = 0.0f;  // hPa dari BMP280
    float altitude    = 0.0f;  // meter

    bool  ahtOnline   = false;
    bool  bmpOnline   = false;

    const char* weatherIcon = "⛅";
    const char* weatherText = "---";

private:
    Adafruit_AHTX0   _aht;
    Adafruit_BMP280  _bmp;

    // Histori tekanan untuk prediksi cuaca
    float _pressHistory[PRESS_HISTORY] = {};
    int   _pressIdx = 0;
    int   _pressCnt = 0;
    unsigned long _lastPressRecord = 0;

public:
    void begin() {
        // AHT20
        if (_aht.begin()) {
            ahtOnline = true;
            Serial.println("[AHT20] OK");
        } else {
            Serial.println("[AHT20] Tidak ditemukan");
        }

        // BMP280
        Wire.beginTransmission(ADDR_BMP280_1);
        uint8_t addr = (Wire.endTransmission() == 0) ? ADDR_BMP280_1 : ADDR_BMP280_2;
        if (_bmp.begin(addr)) {
            _bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                             Adafruit_BMP280::SAMPLING_X2,
                             Adafruit_BMP280::SAMPLING_X16,
                             Adafruit_BMP280::FILTER_X16,
                             Adafruit_BMP280::STANDBY_MS_500);
            bmpOnline = true;
            Serial.printf("[BMP280] OK addr=0x%02X\n", addr);
        } else {
            Serial.println("[BMP280] Tidak ditemukan");
        }
    }

    void read() {
        // AHT20
        if (ahtOnline) {
            sensors_event_t h, t;
            _aht.getEvent(&h, &t);
            float rawT = t.temperature + nvs.getAhtTempOffset();
            float rawH = h.relative_humidity + nvs.getAhtHumOffset();
            if (rawT > -40.0f && rawT < 85.0f) temperature = rawT;
            if (rawH >= 0.0f && rawH <= 100.0f) humidity = rawH;
        }

        // BMP280
        if (bmpOnline) {
            float rawP = _bmp.readPressure() / 100.0f;
            float rawT = _bmp.readTemperature() + nvs.getBmpTempOffset();
            if (rawP > 800.0f && rawP < 1100.0f) {
                pressure = rawP;
                altitude = _bmp.readAltitude(1013.25f);
            }
            // Pakai suhu BMP jika AHT tidak online
            if (!ahtOnline && rawT > -40.0f && rawT < 85.0f) temperature = rawT;
        }

        // Rekam histori tekanan tiap 5 menit
        unsigned long now = millis();
        if (bmpOnline && pressure > 0.0f &&
            (now - _lastPressRecord >= 300000UL || _pressCnt == 0)) {
            _pressHistory[_pressIdx] = pressure;
            _pressIdx = (_pressIdx + 1) % PRESS_HISTORY;
            if (_pressCnt < PRESS_HISTORY) _pressCnt++;
            _lastPressRecord = now;
            _updateWeather();
        }
    }

private:
    void _updateWeather() {
        if (_pressCnt < 2) { weatherText = "---"; weatherIcon = "⛅"; return; }

        // Tren tekanan: bandingkan rata-rata 1 jam terakhir vs 2 jam sebelumnya
        float recent = 0, older = 0;
        int r = 0, o = 0;
        for (int i = 0; i < _pressCnt; i++) {
            int idx = (_pressIdx - 1 - i + PRESS_HISTORY) % PRESS_HISTORY;
            if (i < 12) { recent += _pressHistory[idx]; r++; }
            else if (i < 24) { older  += _pressHistory[idx]; o++; }
        }
        if (r == 0) return;
        float avgR = recent / r;
        float trend = (o > 0) ? (avgR - older / o) : 0.0f;

        // Klasifikasi berdasarkan tekanan mutlak + tren
        if      (avgR >= 1022.0f && trend >= 0.0f) { weatherText = "Cerah";   weatherIcon = "☀️"; }
        else if (avgR >= 1013.0f && trend >= -0.5f){ weatherText = "Berawan"; weatherIcon = "⛅"; }
        else if (avgR >= 1000.0f && trend >= -1.0f){ weatherText = "Mendung"; weatherIcon = "☁️"; }
        else if (avgR >= 990.0f  || trend < -1.0f) { weatherText = "Hujan";   weatherIcon = "🌧️"; }
        else                                         { weatherText = "Badai";   weatherIcon = "⛈️"; }
    }
};

EnvSensor envSensor;
