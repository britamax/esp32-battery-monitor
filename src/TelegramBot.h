#pragma once

// ============================================================
// BattAnalyzer — TelegramBot.h
// Universal Arduino Telegram Bot — polling + notifikasi
// ============================================================

#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include "Config.h"
#include "NVSManager.h"
#include "Logger.h"
#include "BatteryMonitor.h"
#include "EnvSensor.h"
#include "SeismicSensor.h"
#include "RelayManager.h"
#include "BuzzerManager.h"

class TelegramBot {
public:
    bool isEnabled = false;

private:
    WiFiClientSecure* _client  = nullptr;
    UniversalTelegramBot* _bot = nullptr;
    unsigned long _lastPoll    = 0;
    String        _chatId;
    bool          _initialized = false;

public:
    void begin() {
        if (!nvs.getTeleEn() || nvs.getTeleToken().length() < 10) return;
        _chatId = nvs.getTeleChatId();

        _client = new WiFiClientSecure();
        _client->setInsecure();
        _bot    = new UniversalTelegramBot(nvs.getTeleToken(), *_client);

        isEnabled    = true;
        _initialized = true;
        Serial.println("[TELE] Bot inisialisasi OK");

        // Notifikasi boot
        sendNotif("🟢 *BattAnalyzer Online*\n"
                  "Perangkat berhasil boot dan terhubung ke jaringan.");
    }

    void handle() {
        if (!isEnabled || !_initialized || WiFi.status() != WL_CONNECTED) return;
        unsigned long now = millis();
        if (now - _lastPoll < 5000) return;
        _lastPoll = now;

        int n = _bot->getUpdates(_bot->last_message_received + 1);
        for (int i = 0; i < n; i++) {
            _processMessage(_bot->messages[i]);
        }
    }

    void sendNotif(const String& msg) {
        if (!isEnabled || !_initialized || _chatId.isEmpty()) return;
        if (WiFi.status() != WL_CONNECTED) return;
        _bot->sendMessage(_chatId, msg, "Markdown");
        String ts = ntpMgr.getTimeStr();
        logger.add(LOG_TELE, ("Notif: " + msg.substring(0, 40)).c_str(),
                   ts.length() ? ts.c_str() : nullptr);
    }

    void sendAlert(const String& type, const String& detail) {
        String msg = "⚠️ *ALERT: " + type + "*\n" + detail + "\n_" + ntpMgr.getTimeStr() + "_";
        sendNotif(msg);
    }

    void reload() {
        isEnabled    = false;
        _initialized = false;
        if (_bot)    { delete _bot;    _bot    = nullptr; }
        if (_client) { delete _client; _client = nullptr; }
        begin();
    }

private:
    void _processMessage(telegramMessage& msg) {
        String text   = msg.text;
        String fromId = msg.chat_id;

        // Whitelist chat_id
        if (!_chatId.isEmpty() && fromId != _chatId) {
            _bot->sendMessage(fromId, "❌ Akses ditolak.", "");
            return;
        }

        String ts = ntpMgr.getTimeStr();
        logger.add(LOG_TELE, ("CMD: " + text).c_str(), ts.length() ? ts.c_str() : nullptr);

        if      (text == "/start" || text == "/help")  _cmdHelp(fromId);
        else if (text == "/status")                     _cmdStatus(fromId);
        else if (text == "/baterai")                    _cmdBatt(fromId);
        else if (text == "/cuaca")                      _cmdEnv(fromId);
        else if (text == "/gempa")                      _cmdSeismic(fromId);
        else if (text == "/alarm off")                  _cmdAlarmOff(fromId);
        else if (text == "/reboot")                     _cmdReboot(fromId);
        else if (text.startsWith("/relay")) {
            // /relay 1 on | /relay 1 off
            int idx; char cmd[8];
            if (sscanf(text.c_str(), "/relay %d %7s", &idx, cmd) == 2) {
                if (idx >= 1 && idx <= RELAY_COUNT) {
                    bool on = (String(cmd) == "on" || String(cmd) == "ON");
                    relayMgr.set(idx - 1, on);
                    _bot->sendMessage(fromId,
                        "✅ Relay " + String(idx) + " " + (on ? "ON" : "OFF"), "");
                }
            }
        }
        else {
            _bot->sendMessage(fromId, "❓ Command tidak dikenal. Ketik /help", "");
        }
    }

    void _cmdHelp(const String& chatId) {
        String msg = "📋 *BattAnalyzer Commands*\n\n"
                     "/status — Ringkasan lengkap\n"
                     "/baterai — Detail baterai\n"
                     "/cuaca — Kondisi lingkungan\n"
                     "/gempa — Status seismik\n"
                     "/relay 1-4 on|off — Kontrol relay\n"
                     "/alarm off — Matikan buzzer\n"
                     "/reboot — Restart perangkat";
        _bot->sendMessage(chatId, msg, "Markdown");
    }

    void _cmdStatus(const String& chatId) {
        char buf[400];
        snprintf(buf, sizeof(buf),
            "📊 *Status Lengkap*\n\n"
            "🔋 Baterai: %.2fV %.0f%%  %s\n"
            "⚡ Arus: %+.2fA  Daya: %.2fW\n"
            "🌡️ Suhu: %.1f°C  RH: %.0f%%\n"
            "📡 MQTT: %s\n"
            "⏱️ Uptime: %lus",
            battMon.voltage, battMon.soc, battMon.status,
            battMon.current, battMon.power,
            envSensor.temperature, envSensor.humidity,
            mqttMgr.isConnected ? "OK" : "OFF",
            ntpMgr.synced ? (unsigned long)(millis() / 1000) : 0UL);
        _bot->sendMessage(chatId, String(buf), "Markdown");
    }

    void _cmdBatt(const String& chatId) {
        char buf[300];
        snprintf(buf, sizeof(buf),
            "🔋 *Detail Baterai*\n\n"
            "Tegangan: %.3fV\n"
            "Arus: %+.3fA\n"
            "Daya: %.2fW\n"
            "SoC: %.0f%%\n"
            "Status: %s\n"
            "Charge: %.0fmAh / %.2fWh\n"
            "Discharge: %.0fmAh / %.2fWh\n"
            "Siklus: %d",
            battMon.voltage, battMon.current, battMon.power,
            battMon.soc, battMon.status,
            battMon.mahCharge, battMon.whCharge,
            battMon.mahDischarge, battMon.whDischarge,
            battMon.cycleCount);
        _bot->sendMessage(chatId, String(buf), "Markdown");
    }

    void _cmdEnv(const String& chatId) {
        char buf[200];
        snprintf(buf, sizeof(buf),
            "🌡️ *Lingkungan*\n\n"
            "Suhu: %.1f°C\n"
            "Kelembaban: %.1f%%\n"
            "Tekanan: %.1f hPa\n"
            "Ketinggian: %.0f m\n"
            "Cuaca: %s %s",
            envSensor.temperature, envSensor.humidity,
            envSensor.pressure, envSensor.altitude,
            envSensor.weatherIcon, envSensor.weatherText);
        _bot->sendMessage(chatId, String(buf), "Markdown");
    }

    void _cmdSeismic(const String& chatId) {
        char buf[200];
        snprintf(buf, sizeof(buf),
            "🌍 *Seismik*\n\n"
            "Status: %s\n"
            "Magnitude: %.4fg\n"
            "AccX: %.3fg  AccY: %.3fg  AccZ: %.3fg\n"
            "Kalibrasi: %s",
            seismic.statusText(), seismic.magnitude,
            seismic.accX, seismic.accY, seismic.accZ,
            seismic.isCalibrated ? "✅" : "❌");
        _bot->sendMessage(chatId, String(buf), "Markdown");
    }

    void _cmdAlarmOff(const String& chatId) {
        buzzer.stop();
        _bot->sendMessage(chatId, "🔕 Alarm dimatikan.", "");
    }

    void _cmdReboot(const String& chatId) {
        _bot->sendMessage(chatId, "🔄 Restart dalam 3 detik...", "");
        delay(3000);
        ESP.restart();
    }
};

TelegramBot teleBot;
