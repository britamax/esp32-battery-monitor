#pragma once

// ============================================================
// BattAnalyzer — SeismicSensor.h
// ADXL345 akselerometer — hardware interrupt INT1 → ISR → Queue
// ============================================================

#include <Wire.h>
#include <Adafruit_ADXL345_U.h>
#include "Config.h"
#include "NVSManager.h"

// Queue untuk komunikasi ISR → main task
static QueueHandle_t seismicQueue = nullptr;

// ISR — harus IRAM_ATTR
void IRAM_ATTR seismicISR() {
    uint8_t val = 1;
    if (seismicQueue) xQueueSendFromISR(seismicQueue, &val, nullptr);
}

class SeismicSensor {
public:
    float  accX = 0.0f, accY = 0.0f, accZ = 0.0f;   // g
    float  magnitude = 0.0f;   // total acceleration magnitude - 1g
    bool   isOnline   = false;
    bool   alertTriggered = false;
    bool   isCalibrated   = false;

    // Offset kalibrasi
    float offX = 0.0f, offY = 0.0f, offZ = 0.0f;

private:
    Adafruit_ADXL345_Unified _adxl;
    float _threshold = DEF_SEISMIC_THR;

public:
    SeismicSensor() : _adxl(12345) {}

    void begin() {
        isOnline = false;
        seismicQueue = xQueueCreate(10, sizeof(uint8_t));

        Wire.beginTransmission(ADDR_ADXL345);
        if (Wire.endTransmission() != 0) {
            Serial.println("[ADXL345] Tidak ditemukan");
            return;
        }
        if (!_adxl.begin()) {
            Serial.println("[ADXL345] Init gagal");
            return;
        }

        _adxl.setRange(ADXL345_RANGE_16_G);
        _adxl.setDataRate(ADXL345_DATARATE_100_HZ);

        // Konfigurasi Activity interrupt
        _threshold = nvs.getSeismicThr();
        // ADXL345 threshold register: 1 LSB = 62.5mg
        uint8_t thrReg = (uint8_t)(_threshold / 0.0625f);
        if (thrReg < 1) thrReg = 1;

        // Load kalibrasi jika ada
        if (nvs.isSeismicCal()) {
            offX = nvs.getSeismicCalX();
            offY = nvs.getSeismicCalY();
            offZ = nvs.getSeismicCalZ();
            isCalibrated = true;
        }

        // Setup interrupt pada INT1
        pinMode(PIN_ADXL_INT, INPUT);
        attachInterrupt(digitalPinToInterrupt(PIN_ADXL_INT), seismicISR, RISING);

        // Enable Activity interrupt
        _adxl.writeRegister(ADXL345_REG_THRESH_ACT, thrReg);
        _adxl.writeRegister(0x27, 0xFF);   // ACT_INACT_CTL: enable AC coupling semua axis
        _adxl.writeRegister(0x2E, 0x10);   // INT_ENABLE: Activity bit
        _adxl.writeRegister(0x2F, 0x00);   // INT_MAP: semua ke INT1

        delay(100);
        isOnline = true;
        Serial.printf("[ADXL345] OK, threshold=%.2fg\n", _threshold);
    }

    // Panggil dari sensor task — baca akselerasi
    void read() {
        if (!isOnline) return;
        sensors_event_t ev;
        _adxl.getEvent(&ev);
        accX = ev.acceleration.x / 9.81f - offX;
        accY = ev.acceleration.y / 9.81f - offY;
        accZ = ev.acceleration.z / 9.81f - offZ;
        // Magnitude delta dari 1g
        float total = sqrtf(accX*accX + accY*accY + accZ*accZ);
        magnitude = fabsf(total - 1.0f);
    }

    // Cek queue dari ISR — panggil dari alert task
    bool checkAlert() {
        uint8_t val;
        if (!seismicQueue) return false;
        if (xQueueReceive(seismicQueue, &val, 0) == pdTRUE) {
            read();  // baca data saat interrupt
            alertTriggered = (magnitude >= _threshold);
            // Clear interrupt dengan membaca INT_SOURCE register
            _adxl.readRegister(ADXL345_REG_INT_SOURCE);
            return alertTriggered;
        }
        return false;
    }

    void setThreshold(float g) {
        _threshold = g;
        if (isOnline) {
            uint8_t thrReg = (uint8_t)(g / 0.0625f);
            if (thrReg < 1) thrReg = 1;
            _adxl.writeRegister(ADXL345_REG_THRESH_ACT, thrReg);
        }
    }

    // Kalibrasi zero-g — ambil rata-rata saat diam
    bool calibrate(int samples = 200) {
        if (!isOnline) return false;
        float sx = 0, sy = 0, sz = 0;
        for (int i = 0; i < samples; i++) {
            sensors_event_t ev;
            _adxl.getEvent(&ev);
            sx += ev.acceleration.x / 9.81f;
            sy += ev.acceleration.y / 9.81f;
            sz += ev.acceleration.z / 9.81f;
            delay(5);
        }
        offX = sx / samples;
        offY = sy / samples;
        offZ = (sz / samples) - 1.0f;  // Z harus 1g saat datar
        isCalibrated = true;
        nvs.saveSeismicCal(offX, offY, offZ);
        Serial.printf("[ADXL] Cal: X=%.4f Y=%.4f Z=%.4f\n", offX, offY, offZ);
        return true;
    }

    const char* statusText() {
        if (magnitude < 0.05f) return "AMAN";
        if (magnitude < 0.2f)  return "GETAR";
        if (magnitude < 0.5f)  return "SEDANG";
        if (magnitude < 1.0f)  return "KUAT";
        return "GEMPA!";
    }
};

SeismicSensor seismic;
