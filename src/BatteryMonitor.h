#pragma once

// ============================================================
// BattAnalyzer — BatteryMonitor.h
// Driver INA226 via library INA226_WE
// Akumulasi mAh/Wh, SoC, internal resistance, sparkline 60pt
// ============================================================

#include <Wire.h>
#include <INA226_WE.h>
#include "Config.h"
#include "NVSManager.h"

#define SPARKLINE_SIZE  60   // 60 titik data tren

class BatteryMonitor {
public:
    // Nilai real-time
    float voltage    = 0.0f;   // V
    float current    = 0.0f;   // A (+ = charge, - = discharge)
    float power      = 0.0f;   // W
    float soc        = 0.0f;   // %
    float internalR  = 0.0f;   // mΩ (estimasi)

    // Akumulasi
    float mahCharge    = 0.0f;
    float mahDischarge = 0.0f;
    float whCharge     = 0.0f;
    float whDischarge  = 0.0f;
    int   cycleCount   = 0;
    float runtimeMin   = 0.0f; // estimasi menit tersisa

    // Status
    bool  isOnline     = false;
    const char* status = "IDLE";   // "CHARGING" | "DISCHARGING" | "IDLE"

    // Sparkline 60 titik tegangan & arus
    float sparkV[SPARKLINE_SIZE] = {};
    float sparkI[SPARKLINE_SIZE] = {};
    int   sparkIdx = 0;
    bool  sparkFull = false;

private:
    INA226_WE _ina;
    float  _prevV    = 0.0f;
    float  _prevI    = 0.0f;
    bool   _prevValid = false;
    float  _shunt    = INA226_SHUNT_OHMS;

    // Moving average 8 sampel
    static const int MA = 8;
    float _vBuf[MA] = {};
    float _iBuf[MA] = {};
    int   _maIdx   = 0;
    int   _maCnt   = 0;

public:
    BatteryMonitor() : _ina(ADDR_INA226) {}

    void begin() {
        isOnline = false;
        // Cek keberadaan sensor
        Wire.beginTransmission(ADDR_INA226);
        if (Wire.endTransmission() != 0) {
            Serial.println("[INA226] Tidak ditemukan!");
            return;
        }
        _shunt = nvs.getShuntOhms();
        _ina.init();
        _ina.setResistorRange(_shunt, INA226_MAX_AMPS);
        _ina.setAverage(INA226_AVERAGE_16);
        _ina.setConversionTime(INA226_CONV_TIME_1100, INA226_CONV_TIME_1100);
        _ina.setMeasureMode(INA226_CONTINUOUS);
        delay(200);
        isOnline = true;
        Serial.printf("[INA226] OK, shunt=%.4fΩ\n", _shunt);
    }

    void setShunt(float ohms) {
        if (ohms <= 0.0f) return;
        _shunt = ohms;
        if (isOnline) {
            _ina.setResistorRange(_shunt, INA226_MAX_AMPS);
        }
    }

    void read() {
        if (!isOnline) return;

        float rawV = _ina.getBusVoltage_V();
        float rawI = _ina.getCurrent_A();

        // Guard — nilai tidak wajar
        if (rawV < 0.0f || rawV > 60.0f) return;
        if (fabsf(rawI) > 30.0f) return;

        // Moving average
        _vBuf[_maIdx] = rawV;
        _iBuf[_maIdx] = rawI;
        _maIdx = (_maIdx + 1) % MA;
        if (_maCnt < MA) _maCnt++;

        float sumV = 0, sumI = 0;
        for (int i = 0; i < _maCnt; i++) { sumV += _vBuf[i]; sumI += _iBuf[i]; }
        voltage = sumV / _maCnt;
        current = sumI / _maCnt;
        power   = voltage * current;

        // Status
        if      (current >  0.05f) status = "CHARGING";
        else if (current < -0.05f) status = "DISCHARGING";
        else                        status = "IDLE";

        // SoC estimasi dari voltage (Li-Ion single cell)
        soc = _voltageToSoc(voltage);

        // Internal resistance: ΔV/ΔI saat ada perubahan arus signifikan
        if (_prevValid && fabsf(current - _prevI) > 0.1f) {
            float r = fabsf((voltage - _prevV) / (current - _prevI)) * 1000.0f; // mΩ
            if (r > 0.0f && r < 500.0f) internalR = r * 0.1f + internalR * 0.9f; // EMA
        }
        _prevV = voltage; _prevI = current; _prevValid = true;

        // Runtime estimate (menit tersisa)
        if (current < -0.05f && soc > 0.0f) {
            float capRem = (soc / 100.0f) * nvs.getCapNominal(); // mAh tersisa
            runtimeMin = capRem / fabsf(current * 1000.0f) * 60.0f;
        } else {
            runtimeMin = 0.0f;
        }
    }

    // Akumulasi — panggil tiap INTERVAL_ACCUM_MS
    void accumulate(float intervalSec) {
        if (!isOnline || intervalSec <= 0.0f || intervalSec > 15.0f) return;
        float factorH = intervalSec / 3600.0f;

        if (current > 0.05f) {
            mahCharge += current * 1000.0f * factorH;
            whCharge  += power   * factorH;
        } else if (current < -0.05f) {
            mahDischarge += fabsf(current) * 1000.0f * factorH;
            whDischarge  += fabsf(power)   * factorH;
        }
    }

    // Simpan sparkline tiap baca sensor
    void updateSparkline() {
        sparkV[sparkIdx] = voltage;
        sparkI[sparkIdx] = current;
        sparkIdx = (sparkIdx + 1) % SPARKLINE_SIZE;
        if (!sparkFull && sparkIdx == 0) sparkFull = true;
    }

    // JSON sparkline untuk dashboard
    String sparklineJson() {
        String jv = "[", ji = "[";
        int total = sparkFull ? SPARKLINE_SIZE : sparkIdx;
        for (int i = 0; i < total; i++) {
            int idx = (sparkIdx - total + i + SPARKLINE_SIZE) % SPARKLINE_SIZE;
            if (i > 0) { jv += ","; ji += ","; }
            jv += String(sparkV[idx], 2);
            ji += String(sparkI[idx], 3);
        }
        return "{\"v\":" + jv + "],\"i\":" + ji + "]}";
    }

    void loadAccum() {
        mahCharge    = nvs.getMahCharge();
        mahDischarge = nvs.getMahDischarge();
        whCharge     = nvs.getWhCharge();
        whDischarge  = nvs.getWhDischarge();
        cycleCount   = nvs.getCycleCount();
        // Sanity check
        if (mahCharge > 999999.0f || mahCharge < 0.0f) mahCharge = 0.0f;
        if (mahDischarge > 999999.0f || mahDischarge < 0.0f) mahDischarge = 0.0f;
        if (whCharge > 999999.0f || whCharge < 0.0f) whCharge = 0.0f;
        if (whDischarge > 999999.0f || whDischarge < 0.0f) whDischarge = 0.0f;
        if (cycleCount < 0 || cycleCount > 10000) cycleCount = 0;
    }

private:
    float _voltageToSoc(float v) {
        if (v >= 4.20f) return 100.0f;
        if (v >= 4.10f) return 90.0f;
        if (v >= 4.00f) return 75.0f;
        if (v >= 3.90f) return 60.0f;
        if (v >= 3.80f) return 45.0f;
        if (v >= 3.75f) return 30.0f;
        if (v >= 3.70f) return 20.0f;
        if (v >= 3.65f) return 10.0f;
        return 5.0f;
    }
};

BatteryMonitor battMon;