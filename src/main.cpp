/*
 * ============================================================
 * BattAnalyzer — main.cpp v1.0.0
 *
 * Board   : ESP32-C3 Super Mini (RISC-V 160MHz)
 * I2C     : SDA=GPIO10, SCL=GPIO20
 * Buzzer  : GPIO1  (passive PWM, active HIGH)
 * Button  : GPIO3  (INPUT_PULLUP)
 * Relay   : GPIO0,4,5,6 (active HIGH)
 * ADXL INT: GPIO2  (hardware interrupt RISING)
 *
 * CATATAN PENTING ESP32-C3:
 *   - GPIO8 = onboard LED (active LOW) — JANGAN digunakan
 *   - GPIO9 = BOOT button — JANGAN digunakan
 *   - Single-core RISC-V — semua FreeRTOS task di Core 0
 *   - USB CDC wajib: ARDUINO_USB_CDC_ON_BOOT=1
 * ============================================================
 */

#include <Arduino.h>
#include <Wire.h>

// ── Urutan include SANGAT PENTING ─────────────────────────────
#include "Config.h"
#include "NVSManager.h"
#include "Logger.h"
#include "SessionManager.h"
#include "NTPManager.h"
#include "BuzzerManager.h"
#include "RelayManager.h"
#include "OLEDDisplay.h"
#include "ButtonHandler.h"
#include "BatteryMonitor.h"
#include "EnvSensor.h"
#include "SeismicSensor.h"
#include "WiFiManager.h"
#include "MQTTManager.h"
#include "TelegramBot.h"
#include "WebServer.h"

// ── Timing ────────────────────────────────────────────────────
static unsigned long lastSensor    = 0;
static unsigned long lastAccum     = 0;
static unsigned long lastSave      = 0;
static unsigned long lastUptime    = 0;
static unsigned long lastRelay     = 0;
static unsigned long lastWsPush    = 0;
static unsigned long lastWifiCheck = 0;

unsigned long uptimeSec = 0;

// ── Flags lintas modul ────────────────────────────────────────
volatile bool seismicCalReq = false;
bool          mqttReloadReq = false;
bool          ntpReloadReq  = false;

// ── FreeRTOS Tasks ────────────────────────────────────────────
// ESP32-C3 = single-core RISC-V, semua task di Core 0

void taskNetwork(void* param) {
    Serial.println("[TASK-NET] Dimulai");
    for (int i = 0; i < 30 && !wifiMgr.isConnected; i++)
        vTaskDelay(pdMS_TO_TICKS(500));

    mqttMgr.begin();
    teleBot.begin();

    for (;;) {
        if (wifiMgr.isConnected) {
            if (mqttReloadReq) {
                mqttReloadReq = false;
                mqttMgr.reload();
            }
            mqttMgr.handle();
            teleBot.handle();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ── Cooldown tracker per alarm event ─────────────────────────
static unsigned long _alarmLastFired[ALARM_COUNT] = {0};

static bool _alarmCooldownOk(int eventIdx) {
    unsigned long coolMs = (unsigned long)nvs.getAlarmCool(eventIdx) * 60000UL;
    unsigned long now = millis();
    if (now - _alarmLastFired[eventIdx] >= coolMs) {
        _alarmLastFired[eventIdx] = now;
        return true;
    }
    return false;
}

static void _fireAlarm(int eventIdx, BuzzPattern defPattern,
                        const String& mqttType, const String& mqttDetail,
                        const String& teleType, const String& teleDetail,
                        const char* logMsg) {
    if (!nvs.getAlarmEn(eventIdx)) return;
    if (!_alarmCooldownOk(eventIdx)) return;

    // Buzzer
    int buzz = nvs.getAlarmBuzz(eventIdx);
    if (buzz > 0 && !btnHandler.isAlarmSnoozed()) {
        buzzer.startAlarm((BuzzPattern)buzz);
    }
    // MQTT
    if (nvs.getAlarmMqtt(eventIdx)) {
        mqttMgr.publishAlert(mqttType, mqttDetail);
    }
    // Telegram
    if (nvs.getAlarmTele(eventIdx)) {
        teleBot.sendAlert(teleType, teleDetail);
    }
    // Log
    String ts = ntpMgr.getTimeStr();
    logger.add(LOG_ALERT, logMsg, ts.length() ? ts.c_str() : nullptr);
}

void taskAlert(void* param) {
    logger.add(LOG_SYSTEM, "taskAlert dimulai", nullptr);
    vTaskDelay(pdMS_TO_TICKS(3000));

    for (;;) {
        // ── Seismik ───────────────────────────────────────────
        if (seismic.isOnline && seismic.checkAlert()) {
            char detail[80];
            snprintf(detail, sizeof(detail),
                "Magnitude: %.4fg | Status: %s",
                seismic.magnitude, seismic.statusText());
            char logbuf[80];
            snprintf(logbuf, sizeof(logbuf),
                "GEMPA: %.4fg — %s", seismic.magnitude, seismic.statusText());

            _fireAlarm(ALARM_QUAKE, BUZZ_QUAKE,
                "GEMPA", "mag=" + String(seismic.magnitude, 4),
                "GEMPA", String(detail),
                logbuf);
        }

        float vbat = battMon.voltage;
        float ibat = battMon.current;

        if (vbat > 0.5f) {
            // ── Tegangan rendah ───────────────────────────────
            if (vbat < nvs.getVCutoff()) {
                char detail[60];
                snprintf(detail, sizeof(detail),
                    "V: %.3fV | Cutoff: %.2fV", vbat, nvs.getVCutoff());
                char logbuf[60];
                snprintf(logbuf, sizeof(logbuf), "Tegangan rendah: %.3fV", vbat);

                _fireAlarm(ALARM_VOLT_LOW, BUZZ_BATT_LOW,
                    "VOLT_LOW", "v=" + String(vbat, 3),
                    "Tegangan Rendah", String(detail),
                    logbuf);

                // Auto cutoff relay 2
                if (nvs.getCutoffEn() && !relayMgr.getCutoffState()) {
                    relayMgr.setCutoff(true);
                    logger.add(LOG_ALERT, "Auto cutoff aktif (R2)",
                        ntpMgr.getTimeStr().c_str());
                }
            }

            // ── Tegangan tinggi ───────────────────────────────
            if (vbat > nvs.getVMax()) {
                char detail[60];
                snprintf(detail, sizeof(detail),
                    "V: %.3fV | Max: %.2fV", vbat, nvs.getVMax());
                char logbuf[60];
                snprintf(logbuf, sizeof(logbuf), "Tegangan tinggi: %.3fV", vbat);

                _fireAlarm(ALARM_VOLT_HIGH, BUZZ_BATT_LOW,
                    "VOLT_HIGH", "v=" + String(vbat, 3),
                    "Tegangan Tinggi", String(detail),
                    logbuf);
            }

            // ── Arus berlebih ─────────────────────────────────
            if (fabsf(ibat) > nvs.getIMax()) {
                char detail[60];
                snprintf(detail, sizeof(detail),
                    "I: %.3fA | Max: %.2fA", ibat, nvs.getIMax());
                char logbuf[60];
                snprintf(logbuf, sizeof(logbuf), "Arus berlebih: %.3fA", ibat);

                _fireAlarm(ALARM_OVERCURR, BUZZ_OVERCURR,
                    "OVERCURR", "i=" + String(ibat, 3),
                    "Arus Berlebih", String(detail),
                    logbuf);
            }

            // ── Suhu tinggi ───────────────────────────────────
            if (envSensor.ahtOnline && envSensor.temperature > 50.0f) {
                char detail[60];
                snprintf(detail, sizeof(detail),
                    "Suhu: %.1f°C", envSensor.temperature);
                _fireAlarm(ALARM_TEMP_HIGH, BUZZ_SIRENE,
                    "TEMP_HIGH", "t=" + String(envSensor.temperature, 1),
                    "Suhu Tinggi", String(detail),
                    "Suhu sensor tinggi!");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(800);
    Serial.println("\n========================================");
    Serial.println("  BattAnalyzer v" FW_VERSION);
    Serial.println("  ESP32-C3 Super Mini");
    Serial.println("========================================");

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(400000);
    Serial.println("[I2C] SDA=10 SCL=20 @ 400kHz");

    nvs.begin();
    Serial.println("[NVS] OK");
    logger.setMask(nvs.getLogMask());  // load log group mask dari NVS

    if (oled.begin()) {
        oled.setBrightness(nvs.getOledBright());
        oled.setEnabled(nvs.getOledOn());
        oled.autoScroll = nvs.getOledScroll();
        oled.pageDurMs  = nvs.getOledDur();
        if (!oled.autoScroll) oled.setPage(nvs.getOledPage());
        oled.showSplash(nvs.getDeviceName());
        Serial.println("[OLED] OK");
    } else {
        Serial.println("[OLED] Tidak ditemukan");
    }

    buzzer.begin();
    Serial.println("[BUZZ] OK");

    relayMgr.begin();
    Serial.println("[RELAY] OK");

    btnHandler.begin();
    btnHandler.checkBootHold();

    battMon.begin();
    if (battMon.isOnline) {
        battMon.loadAccum();
        logger.addf(LOG_SENSOR, nullptr,
            "INA226 OK — Chg=%.1fmAh Dis=%.1fmAh SoH=%.0f%%",
            battMon.mahCharge, battMon.mahDischarge, battMon.soh);
    }

    envSensor.begin();
    seismic.begin();

    uptimeSec = nvs.getUptime();
    Serial.printf("[UPTIME] Lanjut dari %lu detik\n", uptimeSec);

    oled.showStatus("Koneksi WiFi...", nvs.getWifiSSID(), "");
    wifiMgr.begin();

    oled.wifi.mode   = wifiMgr.isAPMode ? (wifiMgr.isSetupMode ? "SETUP" : "AP") : "STA";
    oled.wifi.ssid   = wifiMgr.isAPMode ? String(AP_SSID) : nvs.getWifiSSID();
    oled.wifi.ip     = wifiMgr.ipAddress;
    oled.wifi.wifiOk = wifiMgr.isConnected || wifiMgr.isAPMode;
    oled.wifi.rssi   = wifiMgr.isConnected ? WiFi.RSSI() : 0;

    if (wifiMgr.isConnected) {
        ntpMgr.begin();
        if (ntpMgr.synced)
            logger.add(LOG_SYSTEM, ("NTP sync OK: " + ntpMgr.getTimeStr()).c_str(),
                       ntpMgr.getTimeStr().c_str());
        else
            logger.add(LOG_SYSTEM, "NTP sync gagal", nullptr);
    }

    webServer.begin();

    buzzer.play(BUZZ_SHORT); delay(150); buzzer.play(BUZZ_SHORT);

    {
        String ts = ntpMgr.getTimeStr();
        logger.addf(LOG_SYSTEM, ts.length() ? ts.c_str() : nullptr,
            "Boot OK — FW v%s | %s", FW_VERSION, FW_BUILD);
        String wMsg = wifiMgr.isConnected
            ? "WiFi OK: " + wifiMgr.ipAddress
            : String(wifiMgr.isSetupMode ? "Setup AP" : "Local AP");
        logger.add(LOG_WIFI, wMsg.c_str(), ts.length() ? ts.c_str() : nullptr);
        logger.addf(LOG_SENSOR, ts.length() ? ts.c_str() : nullptr,
            "INA226:%s AHT20:%s BMP280:%s ADXL:%s",
            battMon.isOnline    ? "OK" : "--",
            envSensor.ahtOnline ? "OK" : "--",
            envSensor.bmpOnline ? "OK" : "--",
            seismic.isOnline    ? "OK" : "--");
    }

    xTaskCreatePinnedToCore(taskNetwork, "taskNet",   6144, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(taskAlert,   "taskAlert", 3072, NULL, 4, NULL, 0);
    Serial.println("[RTOS] Tasks dimulai di Core 0");

    lastSensor = lastAccum = lastSave = lastUptime =
    lastRelay  = lastWsPush = lastWifiCheck = millis();

    Serial.println("[READY] Sistem siap!\n");
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    btnHandler.handle();
    wifiMgr.handle();
    buzzer.handle();

    // OTA via Telegram — harus di main loop (blocking ~60 detik saat flash)
    // processOta() return langsung jika tidak ada OTA pending
    teleBot.processOta();

    // Relay manual toggle dari tombol fisik
    if (relayManualToggle) {
        relayManualToggle = false;
        relayMgr.toggle(0);
        String ts = ntpMgr.getTimeStr();
        logger.addf(LOG_SYSTEM, ts.length() ? ts.c_str() : nullptr,
            "Relay 1 toggle: %s", relayMgr.state[0] ? "ON" : "OFF");
    }

    // Kalibrasi seismik
    if (seismicCalReq) {
        seismicCalReq = false;
        oled.showStatus("Kalibrasi ADXL", "Diam...", "Proses...");
        if (seismic.calibrate(200)) {
            oled.showStatus("Kalibrasi OK!", "", "");
            buzzer.play(BUZZ_SHORT); delay(200); buzzer.play(BUZZ_SHORT);
            String ts = ntpMgr.getTimeStr();
            logger.addf(LOG_SENSOR, ts.length() ? ts.c_str() : nullptr,
                "ADXL cal OK: X=%.4f Y=%.4f Z=%.4f", seismic.offX, seismic.offY, seismic.offZ);
        } else {
            oled.showStatus("Kalibrasi GAGAL", "", "");
            buzzer.play(BUZZ_SIRENE);
            logger.add(LOG_SENSOR, "ADXL345 kalibrasi gagal", nullptr);
        }
        delay(2000);
    }

    // WiFi reconnect check tiap 10 detik
    if (now - lastWifiCheck >= 10000) {
        wifiMgr.checkReconnect();
        if (wifiMgr.isConnected) oled.wifi.rssi = WiFi.RSSI();
        lastWifiCheck = now;
    }

    // NTP / MQTT reload dari WebServer
    if (webServer.ntpReloadReq || ntpReloadReq) {
        webServer.ntpReloadReq = ntpReloadReq = false;
        ntpMgr.applyTimezone();
    }
    if (webServer.mqttReloadReq) {
        webServer.mqttReloadReq = false;
        mqttReloadReq = true;
    }

    // Baca sensor tiap 500ms
    if (now - lastSensor >= INTERVAL_SENSOR_MS) {
        battMon.read();
        battMon.updateSparkline();
        envSensor.read();
        seismic.read();

        // Init SoC dari voltage pada pembacaan pertama
        static bool _socInitDone = false;
        if (!_socInitDone && battMon.voltage > 0.5f) {
            battMon.initSocFromVoltage();
            _socInitDone = true;
        }

        // Cek pergantian hari untuk history
        if (ntpMgr.synced) {
            struct tm ti;
            if (getLocalTime(&ti)) {
                // unix day = seconds/86400
                uint32_t today = (uint32_t)(mktime(&ti) / 86400);
                if (battMon.histLastDay == 0) {
                    battMon.histLastDay = today;
                    nvs.saveHistDay(today);
                } else if (today != battMon.histLastDay) {
                    battMon.shiftHistory();
                    battMon.histLastDay = today;
                    nvs.saveHistDay(today);
                }
            }
        }

        oled.batt.voltage    = battMon.voltage;
        oled.batt.current    = battMon.current;
        oled.batt.soc        = battMon.soc;
        oled.batt.soh        = battMon.soh;
        oled.batt.internalR  = battMon.internalR;
        oled.batt.status     = battMon.status;
        oled.energy.mahChg   = battMon.mahCharge;
        oled.energy.mahDis   = battMon.mahDischarge;
        oled.energy.whChg    = battMon.whCharge;
        oled.energy.whDis    = battMon.whDischarge;
        oled.energy.cycles   = battMon.cycleCount;
        oled.env.temp        = envSensor.temperature;
        oled.env.humidity    = envSensor.humidity;
        oled.env.pressure    = envSensor.pressure;
        oled.env.weather     = envSensor.weatherText;
        oled.seismic.magnitude = seismic.magnitude;
        oled.seismic.status    = seismic.statusText();
        oled.sys.uptime   = uptimeSec;
        oled.sys.version  = String(FW_VERSION);
        oled.sys.mqttOk   = mqttMgr.isConnected;
        oled.sys.ntpOk    = ntpMgr.synced;
        oled.sys.heapKb   = ESP.getFreeHeap() / 1024.0f;

        lastSensor = now;
    }

    oled.update();

    // Akumulasi tiap 10 detik
    if (now - lastAccum >= INTERVAL_ACCUM_MS) {
        float dt = (now - lastAccum) / 1000.0f;
        lastAccum = now;
        battMon.accumulate(dt);
    }

    // Simpan NVS tiap 5 menit
    if (now - lastSave >= INTERVAL_SAVE_MS) {
        nvs.saveEnergy(battMon.mahCharge, battMon.mahDischarge,
                       battMon.whCharge,  battMon.whDischarge,
                       battMon.cycleCount);
        nvs.saveUptime(uptimeSec);
        String ts = ntpMgr.getTimeStr();
        logger.addf(LOG_BATT, ts.length() ? ts.c_str() : nullptr,
            "Save: Chg=%.0fmAh/%.2fWh Dis=%.0fmAh/%.2fWh",
            battMon.mahCharge, battMon.whCharge,
            battMon.mahDischarge, battMon.whDischarge);
        lastSave = now;
    }

    // Cek jadwal relay tiap menit
    if (now - lastRelay >= INTERVAL_RELAY_MS) {
        relayMgr.handleSchedule();
        lastRelay = now;
    }

    // NTP sync tiap 6 jam
    ntpMgr.handle();

    // WebSocket push tiap 1 detik
    if (now - lastWsPush >= 1000) {
        webServer.pushSensorData();
        lastWsPush = now;
    }

    // Uptime counter
    if (now - lastUptime >= INTERVAL_UPTIME_MS) {
        uptimeSec++;
        lastUptime = now;
        if (uptimeSec % 600 == 0) nvs.saveUptime(uptimeSec);
    }

    delay(20);
}