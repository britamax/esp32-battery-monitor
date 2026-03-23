#pragma once

// ============================================================
// BattAnalyzer — BatteryMonitor.h
// Driver INA226 via library INA226_WE
//
// Fitur baru v1.1:
//   - SoC Hybrid: Coulomb Counting + koreksi voltage
//   - State of Health (SoH) dari kapasitas real vs nominal
//   - INA226 advanced config (averaging, conv time, mode)
//   - Kalibrasi offset arus dan tegangan
//   - C-rate, charge efficiency, identifikasi baterai
//   - History harian (7 hari) untuk bar chart
// ============================================================

#include <Wire.h>
#include <INA226_WE.h>
#include "Config.h"
#include "NVSManager.h"

#define SPARKLINE_SIZE  60

// Mapping index ke enum INA226 untuk averaging
static const INA226_AVERAGES _inaAvgTable[] = {
    INA226_AVERAGE_1, INA226_AVERAGE_4, INA226_AVERAGE_16,
    INA226_AVERAGE_64, INA226_AVERAGE_128, INA226_AVERAGE_256,
    INA226_AVERAGE_512, INA226_AVERAGE_1024
};
static const char* _inaAvgLabel[] = {
    "1", "4", "16", "64", "128", "256", "512", "1024"
};
#define INA_AVG_COUNT 8

static const INA226_CONV_TIME _inaConvTable[] = {
    INA226_CONV_TIME_140, INA226_CONV_TIME_204, INA226_CONV_TIME_332,
    INA226_CONV_TIME_588, INA226_CONV_TIME_1100, INA226_CONV_TIME_2116,
    INA226_CONV_TIME_4156, INA226_CONV_TIME_8244
};
static const char* _inaConvLabel[] = {
    "140µs","204µs","332µs","588µs","1.1ms","2.1ms","4.2ms","8.2ms"
};
#define INA_CONV_COUNT 8

static const INA226_MEASURE_MODE _inaModeTable[] = {
    INA226_CONTINUOUS, INA226_TRIGGERED, INA226_POWER_DOWN
};
static const char* _inaModeLabel[] = {
    "Continuous", "Triggered", "Power Down"
};
#define INA_MODE_COUNT 3

class BatteryMonitor {
public:
    // Real-time values
    float voltage    = 0.0f;   // V — tegangan terminal baterai (terkoreksi)
    float current    = 0.0f;   // A (+ = charge, - = discharge)
    float power      = 0.0f;   // W
    float soc        = 50.0f;  // % — State of Charge (hybrid)
    float soh        = 100.0f; // % — State of Health
    float internalR  = 0.0f;   // mΩ — internal resistance EMA
    float cRate      = 0.0f;   // C-rate saat ini
    float runtimeMin = 0.0f;   // estimasi menit tersisa / menuju full

    // Akumulasi
    float mahCharge    = 0.0f;
    float mahDischarge = 0.0f;
    float whCharge     = 0.0f;
    float whDischarge  = 0.0f;
    int   cycleCount   = 0;
    float capReal      = 0.0f;   // kapasitas real terukur (mAh)

    // Status
    bool  isOnline  = false;
    const char* status = "IDLE";

    // Sparkline
    float sparkV[SPARKLINE_SIZE] = {};
    float sparkI[SPARKLINE_SIZE] = {};
    int   sparkIdx  = 0;
    bool  sparkFull = false;

    // History 7 hari
    float histChgMah[HISTORY_DAYS] = {};
    float histDisMah[HISTORY_DAYS] = {};
    float histChgWh[HISTORY_DAYS]  = {};
    float histDisWh[HISTORY_DAYS]  = {};
    uint32_t histLastDay = 0;  // unix day terakhir history di-shift

    // Akumulasi harian (hari ini)
    float todayChgMah = 0.0f;
    float todayDisMah = 0.0f;
    float todayChgWh  = 0.0f;
    float todayDisWh  = 0.0f;

private:
    INA226_WE _ina;
    float  _prevV     = 0.0f;
    float  _prevI     = 0.0f;
    bool   _prevValid = false;
    float  _shunt     = INA226_SHUNT_OHMS;
    float  _maxAmps   = INA226_MAX_AMPS;
    float  _iOffset   = 0.0f;   // kalibrasi offset arus
    float  _vOffset   = 0.0f;   // kalibrasi offset tegangan

    // SoC coulomb tracking
    float  _socCoulomb = 50.0f;  // SoC dari coulomb counting
    bool   _socInited  = false;
    float  _capForSoc  = 2000.0f;  // kapasitas dipakai untuk SoC (mAh)

    // Charge efficiency tracking
    float  _lastFullChgMah  = 0.0f;
    float  _lastFullDisMah  = 0.0f;
    bool   _wasCharging     = false;

    // Moving average 8 sampel
    static const int MA = 8;
    float _vBuf[MA] = {};
    float _iBuf[MA] = {};
    int   _maIdx    = 0;
    int   _maCnt    = 0;

public:
    BatteryMonitor() : _ina(ADDR_INA226) {}

    void begin() {
        isOnline = false;
        Wire.beginTransmission(ADDR_INA226);
        if (Wire.endTransmission() != 0) {
            logger.add(LOG_SENSOR, "INA226 tidak ditemukan!", nullptr);
            return;
        }

        _shunt   = nvs.getShuntOhms();
        _maxAmps = _calcMaxAmps(_shunt);
        _iOffset = nvs.getInaIOffset();
        _vOffset = nvs.getInaVOffset();

        _ina.init();
        _ina.setResistorRange(_shunt, _maxAmps);

        // Terapkan konfigurasi advanced dari NVS
        _applyInaConfig();

        delay(200);
        isOnline = true;

        logger.addf(LOG_SENSOR, nullptr,
            "INA226 OK shunt=%.4fΩ maxA=%.2fA iOff=%.4fA vOff=%.4fV",
            _shunt, _maxAmps, _iOffset, _vOffset);
    }

    // Terapkan averaging, conv time, mode dari NVS
    void _applyInaConfig() {
        int avgIdx   = constrain(nvs.getInaAvg(),   0, INA_AVG_COUNT  - 1);
        int vconvIdx = constrain(nvs.getInaVConv(), 0, INA_CONV_COUNT - 1);
        int iconvIdx = constrain(nvs.getInaIConv(), 0, INA_CONV_COUNT - 1);
        int modeIdx  = constrain(nvs.getInaMode(),  0, INA_MODE_COUNT - 1);

        _ina.setAverage(_inaAvgTable[avgIdx]);
        _ina.setConversionTime(_inaConvTable[vconvIdx], _inaConvTable[iconvIdx]);
        _ina.setMeasureMode(_inaModeTable[modeIdx]);

        logger.addf(LOG_SENSOR, nullptr,
            "INA226 config: avg=%s vconv=%s iconv=%s mode=%s",
            _inaAvgLabel[avgIdx], _inaConvLabel[vconvIdx],
            _inaConvLabel[iconvIdx], _inaModeLabel[modeIdx]);
    }

    // Dipanggil dari WebServer saat shunt diubah
    void setShunt(float ohms) {
        if (ohms <= 0.0f) return;
        _shunt   = ohms;
        _maxAmps = _calcMaxAmps(ohms);
        if (isOnline) {
            _ina.setResistorRange(_shunt, _maxAmps);
            logger.addf(LOG_SENSOR, nullptr,
                "INA226 shunt updated: %.4fΩ maxA=%.2fA", _shunt, _maxAmps);
        }
    }

    // Dipanggil dari WebServer saat INA config diubah
    void reloadInaConfig() {
        _iOffset = nvs.getInaIOffset();
        _vOffset = nvs.getInaVOffset();
        if (isOnline) _applyInaConfig();
    }

    void read() {
        if (!isOnline) return;

        float rawV = _ina.getBusVoltage_V();
        float rawI = _ina.getCurrent_A();

        if (rawV < 0.0f || rawV > 60.0f) return;
        if (fabsf(rawI) > (_maxAmps * 1.1f)) return;

        // Terapkan kalibrasi offset
        rawI -= _iOffset;

        // Koreksi tegangan high-side: V_batt = V_bus + I×Rshunt + V_offset
        float correctedV = rawV + (rawI * _shunt) + _vOffset;

        // Moving average
        _vBuf[_maIdx] = correctedV;
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

        // SoC
        _updateSoC();
        soc = constrain(_socCoulomb, 0.0f, 100.0f);

        // Internal resistance EMA
        if (_prevValid && fabsf(current - _prevI) > 0.1f) {
            float r = fabsf((voltage - _prevV) / (current - _prevI)) * 1000.0f;
            if (r > 0.0f && r < 1000.0f)
                internalR = r * 0.05f + internalR * 0.95f;
        }
        _prevV = voltage; _prevI = current; _prevValid = true;

        // C-rate = I / Capacity_nominal
        float cap = nvs.getCapNominal();
        if (cap > 0.0f) cRate = fabsf(current * 1000.0f) / cap;

        // Runtime estimate
        if (current < -0.05f && soc > 0.0f) {
            float capRem = (soc / 100.0f) * _capForSoc;
            runtimeMin = capRem / fabsf(current * 1000.0f) * 60.0f;
        } else if (current > 0.05f && soc < 100.0f) {
            float capLeft = ((100.0f - soc) / 100.0f) * _capForSoc;
            runtimeMin = capLeft / fabsf(current * 1000.0f) * 60.0f;
        } else {
            runtimeMin = 0.0f;
        }
    }

    void accumulate(float intervalSec) {
        if (!isOnline || intervalSec <= 0.0f || intervalSec > 15.0f) return;
        float factorH = intervalSec / 3600.0f;

        if (current > 0.05f) {
            float dMah = current * 1000.0f * factorH;
            float dWh  = power   * factorH;
            mahCharge += dMah; whCharge += dWh;
            todayChgMah += dMah; todayChgWh += dWh;
            // Coulomb counting: SoC naik
            if (_capForSoc > 0.0f)
                _socCoulomb += (dMah / _capForSoc) * 100.0f;
        } else if (current < -0.05f) {
            float dMah = fabsf(current) * 1000.0f * factorH;
            float dWh  = fabsf(power)   * factorH;
            mahDischarge += dMah; whDischarge += dWh;
            todayDisMah  += dMah; todayDisWh  += dWh;
            // Coulomb counting: SoC turun
            if (_capForSoc > 0.0f)
                _socCoulomb -= (dMah / _capForSoc) * 100.0f;
        }

        _socCoulomb = constrain(_socCoulomb, 0.0f, 100.0f);

        // Deteksi siklus penuh: charge → discharge transisi
        _detectCycleAndCapacity();
    }

    // Geser history satu hari (dipanggil dari main.cpp saat pergantian hari)
    void shiftHistory() {
        // Geser array ke kanan (hari baru masuk di index 0)
        for (int i = HISTORY_DAYS - 1; i > 0; i--) {
            histChgMah[i] = histChgMah[i-1];
            histDisMah[i] = histDisMah[i-1];
            histChgWh[i]  = histChgWh[i-1];
            histDisWh[i]  = histDisWh[i-1];
        }
        // Simpan hari ini ke slot 0
        histChgMah[0] = todayChgMah;
        histDisMah[0] = todayDisMah;
        histChgWh[0]  = todayChgWh;
        histDisWh[0]  = todayDisWh;

        // Reset akumulator harian
        todayChgMah = todayDisMah = todayChgWh = todayDisWh = 0.0f;

        // Simpan ke NVS
        nvs.saveHistChgMah(histChgMah);
        nvs.saveHistDisMah(histDisMah);
        nvs.saveHistChgWh(histChgWh);
        nvs.saveHistDisWh(histDisWh);

        logger.add(LOG_BATT, "History hari baru dimulai", nullptr);
    }

    // Kalibrasi offset arus — baca saat open circuit (I harus = 0)
    void calibrateCurrentOffset() {
        if (!isOnline) return;
        float sum = 0;
        for (int i = 0; i < 100; i++) {
            sum += _ina.getCurrent_A();
            delay(10);
        }
        _iOffset = sum / 100.0f;
        nvs.setInaIOffset(_iOffset);
        logger.addf(LOG_SENSOR, nullptr,
            "INA kalibrasi I offset: %.5fA", _iOffset);
    }

    // Kalibrasi shunt dengan arus aktual dari multimeter
    // actualAmps = nilai dari multimeter eksternal
    void calibrateShuntActual(float actualAmps) {
        if (!isOnline || fabsf(actualAmps) < 0.1f) return;
        float rawI = _ina.getCurrent_A();
        if (fabsf(rawI) < 0.05f) return;  // arus terlalu kecil untuk kalibrasi

        // V_shunt = rawI * shunt_lama
        // Shunt_baru = V_shunt / actualAmps
        float vShunt = rawI * _shunt;
        float newShunt = vShunt / actualAmps;

        if (newShunt > 0.0001f && newShunt < 0.1f) {
            _shunt = newShunt;
            _maxAmps = _calcMaxAmps(_shunt);
            nvs.setShuntOhms(_shunt);
            _ina.setResistorRange(_shunt, _maxAmps);
            logger.addf(LOG_SENSOR, nullptr,
                "INA shunt kalibrasi: %.5fΩ (dari %.5fΩ)",
                _shunt, nvs.getShuntOhms());
        }
    }

    void updateSparkline() {
        sparkV[sparkIdx] = voltage;
        sparkI[sparkIdx] = current;
        sparkIdx = (sparkIdx + 1) % SPARKLINE_SIZE;
        if (!sparkFull && sparkIdx == 0) sparkFull = true;
    }

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

    // JSON history 7 hari untuk dashboard chart
    String historyJson(bool useWh = false) {
        String j = "{\"chg\":[";
        for (int i = HISTORY_DAYS - 1; i >= 0; i--) {
            if (i < HISTORY_DAYS - 1) j += ",";
            j += String(useWh ? histChgWh[i] : histChgMah[i], 1);
        }
        j += "],\"dis\":[";
        for (int i = HISTORY_DAYS - 1; i >= 0; i--) {
            if (i < HISTORY_DAYS - 1) j += ",";
            j += String(useWh ? histDisWh[i] : histDisMah[i], 1);
        }
        j += "],\"unit\":\"";
        j += useWh ? "Wh" : "mAh";
        j += "\"}";
        return j;
    }

    void loadAccum() {
        mahCharge    = nvs.getMahCharge();
        mahDischarge = nvs.getMahDischarge();
        whCharge     = nvs.getWhCharge();
        whDischarge  = nvs.getWhDischarge();
        cycleCount   = nvs.getCycleCount();
        capReal      = nvs.getCapReal();
        soh          = nvs.getSoH();

        if (mahCharge    < 0 || mahCharge    > 999999) mahCharge    = 0;
        if (mahDischarge < 0 || mahDischarge > 999999) mahDischarge = 0;
        if (whCharge     < 0 || whCharge     > 999999) whCharge     = 0;
        if (whDischarge  < 0 || whDischarge  > 999999) whDischarge  = 0;
        if (cycleCount   < 0 || cycleCount   > 10000)  cycleCount   = 0;

        // Load history
        if (!nvs.loadHistChgMah(histChgMah)) memset(histChgMah, 0, sizeof(histChgMah));
        if (!nvs.loadHistDisMah(histDisMah)) memset(histDisMah, 0, sizeof(histDisMah));
        if (!nvs.loadHistChgWh(histChgWh))   memset(histChgWh,  0, sizeof(histChgWh));
        if (!nvs.loadHistDisWh(histDisWh))   memset(histDisWh,  0, sizeof(histDisWh));
        histLastDay = nvs.loadHistDay();

        // Init SoC
        _capForSoc = nvs.getCapNominal();
        int method = nvs.getSocMethod();
        if (method == 0 || !_socInited) {
            // Voltage-based init — akan dikoreksi di read() pertama
            _socCoulomb = nvs.getSocInit();
            _socInited  = true;
        }
    }

    // Dipanggil setelah read() pertama untuk set SoC awal dari voltage
    void initSocFromVoltage() {
        if (voltage < 0.5f) return;
        float vSoc = _voltageToSocRaw(voltage);
        int method = nvs.getSocMethod();
        if (method == 0) {
            // Pure voltage
            _socCoulomb = vSoc;
        } else if (method == 2) {
            // Hybrid: voltage saat awal, lalu coulomb counting
            _socCoulomb = vSoc;
        }
        // method == 1: pure coulomb dari NVS init value
        _socInited = true;
        logger.addf(LOG_BATT, nullptr,
            "SoC init: %.1f%% (method=%d voltage=%.3fV)",
            _socCoulomb, method, voltage);
    }

private:
    void _updateSoC() {
        int method = nvs.getSocMethod();
        if (method == 0) {
            // Pure voltage
            _socCoulomb = _voltageToSocRaw(voltage);
        } else if (method == 2) {
            // Hybrid: di sisi charging penuh atau cutoff — koreksi dari voltage
            float vSoc = _voltageToSocRaw(voltage);
            if (voltage >= nvs.getVMax() * 0.99f) {
                _socCoulomb = 100.0f;  // koreksi saat penuh
            } else if (voltage <= nvs.getVCutoff() * 1.01f) {
                _socCoulomb = 0.0f;    // koreksi saat kosong
            }
            // else: biarkan coulomb counting berjalan
        }
        // method == 1: pure coulomb — tidak ada koreksi voltage
    }

    // Voltage → SoC berdasarkan tipe baterai dari NVS
    // Menggunakan interpolasi linear antara V_cutoff (0%) dan V_max (100%)
    // dengan titik tengah V_nom (50%)
    float _voltageToSocRaw(float v) {
        float vMax    = nvs.getVMax();
        float vNom    = nvs.getVNom();
        float vCutoff = nvs.getVCutoff();

        if (v >= vMax)    return 100.0f;
        if (v <= vCutoff) return 0.0f;

        // Segmen atas: V_nom → V_max = 50% → 100%
        if (v >= vNom) {
            return 50.0f + 50.0f * (v - vNom) / (vMax - vNom);
        }
        // Segmen bawah: V_cutoff → V_nom = 0% → 50%
        return 50.0f * (v - vCutoff) / (vNom - vCutoff);
    }

    void _detectCycleAndCapacity() {
        bool charging    = (current > 0.05f);
        bool discharging = (current < -0.05f);

        // Deteksi akhir discharge (baterai hampir kosong → mulai charge)
        if (!charging && _wasCharging && mahDischarge > 100.0f) {
            // Estimasi kapasitas real dari discharge terakhir
            // Hanya valid jika mulai dari hampir full
            float estimatedCap = mahDischarge - _lastFullDisMah;
            if (estimatedCap > 100.0f && estimatedCap < nvs.getCapNominal() * 1.5f) {
                capReal = capReal * 0.8f + estimatedCap * 0.2f;  // EMA
                nvs.setCapReal(capReal);
                // Update SoH
                soh = constrain((capReal / nvs.getCapNominal()) * 100.0f, 0.0f, 100.0f);
                nvs.setSoH(soh);
                // Update kapasitas untuk SoC
                _capForSoc = capReal > 100.0f ? capReal : nvs.getCapNominal();
                cycleCount++;
                logger.addf(LOG_BATT, nullptr,
                    "Siklus %d selesai: capReal=%.0fmAh SoH=%.1f%%",
                    cycleCount, capReal, soh);
            }
            _lastFullDisMah = mahDischarge;
        }
        _wasCharging = charging;
    }

    float _calcMaxAmps(float ohms) {
        if (ohms <= 0.0f) return INA226_MAX_AMPS;
        float theoretMax = 0.08192f / ohms;
        float safeMax    = theoretMax * 0.80f;
        return constrain(safeMax, 1.0f, 16.0f);
    }
};

BatteryMonitor battMon;