#pragma once

// ============================================================
// BattAnalyzer — TelegramBot.h
// Universal Arduino Telegram Bot
//
// OTA via Telegram (alur benar sesuai library):
//   1. User kirim file .bin ke bot dengan caption "update firmware"
//   2. Library otomatis isi msg.hasDocument=true, msg.file_path,
//      msg.file_size, msg.file_caption
//   3. Bot tanya konfirmasi → user balas /ota confirm
//   4. processOta() dipanggil dari main loop() — download + flash
//      langsung dari msg.file_path (sudah berisi URL CDN Telegram)
//
// Command lain:
//   /ota rollback  — kembali firmware sebelumnya
//   /ota cancel    — batal konfirmasi
//   /ota status    — cek state OTA
//   /debug on|off GRUP — toggle log grup
// ============================================================

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <FS.h>
#include <LittleFS.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "NVSManager.h"
#include "Logger.h"
#include "BatteryMonitor.h"
#include "EnvSensor.h"
#include "SeismicSensor.h"
#include "RelayManager.h"
#include "BuzzerManager.h"

enum OtaState {
    OTA_IDLE      = 0,
    OTA_CONFIRM   = 1,   // menunggu konfirmasi user
    OTA_DOWNLOAD  = 2,   // siap download (dipicu dari main loop)
    OTA_FLASHING  = 3    // sedang flash
};

class TelegramBot {
public:
    bool isEnabled = false;

private:
    WiFiClientSecure*     _client      = nullptr;
    UniversalTelegramBot* _bot         = nullptr;
    unsigned long         _lastPoll    = 0;
    String                _chatId;
    bool                  _initialized = false;

    // OTA state — diisi dari msg.file_path yang sudah disiapkan library
    OtaState  _otaState      = OTA_IDLE;
    String    _otaFilePath;         // URL CDN Telegram dari msg.file_path
    String    _otaFileName;         // nama file dari msg.file_name
    int       _otaFileSize   = 0;   // ukuran dari msg.file_size
    bool      _otaIsFirmware = true; // true=U_FLASH, false=U_SPIFFS
    unsigned long _otaConfirmAt = 0;

public:
    void begin() {
        if (!nvs.getTeleEn() || nvs.getTeleToken().length() < 10) return;
        _chatId = nvs.getTeleChatId();

        _client = new WiFiClientSecure();
        _client->setInsecure();
        _bot = new UniversalTelegramBot(nvs.getTeleToken(), *_client);

        isEnabled    = true;
        _initialized = true;

        String ts = ntpMgr.getTimeStr();
        logger.add(LOG_TELE, "Bot inisialisasi OK", ts.length() ? ts.c_str() : nullptr);

        sendNotif("🟢 *BattAnalyzer Online*\n"
                  "Perangkat berhasil boot.\n"
                  "Ketik /help untuk daftar perintah.");
    }

    void handle() {
        if (!isEnabled || !_initialized || WiFi.status() != WL_CONNECTED) return;
        unsigned long now = millis();
        if (now - _lastPoll < 5000) return;
        _lastPoll = now;

        // Timeout konfirmasi OTA — 60 detik
        if (_otaState == OTA_CONFIRM && now - _otaConfirmAt > 60000) {
            _otaState   = OTA_IDLE;
            _otaFilePath = "";
            sendNotif("⏱️ Konfirmasi OTA timeout. Dibatalkan.");
        }

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
        logger.add(LOG_TELE, ("Notif: " + msg.substring(0, 50)).c_str(),
                   ts.length() ? ts.c_str() : nullptr);
    }

    void sendAlert(const String& type, const String& detail) {
        String msg = "⚠️ *ALERT: " + type + "*\n" + detail +
                     "\n_" + ntpMgr.getTimeStr() + "_";
        sendNotif(msg);
    }

    void reload() {
        isEnabled      = false;
        _initialized   = false;
        _otaState      = OTA_IDLE;
        _otaFilePath   = "";
        _otaIsFirmware = true;
        if (_bot)    { delete _bot;    _bot    = nullptr; }
        if (_client) { delete _client; _client = nullptr; }
        begin();
    }

    // ── OTA — dipanggil dari main loop() saat _otaState == OTA_DOWNLOAD
    void processOta() {
        if (_otaState != OTA_DOWNLOAD) return;
        if (_otaFilePath.isEmpty()) { _otaState = OTA_IDLE; return; }

        _otaState = OTA_FLASHING;

        // URL CDN Telegram: https://api.telegram.org/file/bot<TOKEN>/<file_path>
        String url = "https://api.telegram.org/file/bot" +
                     nvs.getTeleToken() + "/" + _otaFilePath;

        String typeLabel = _otaIsFirmware ? "firmware" : "filesystem";
        sendNotif("⬇️ Download " + typeLabel + " dimulai...\n"
                  "Jangan matikan perangkat!\n"
                  "File: `" + _otaFileName + "` (" +
                  String(_otaFileSize / 1024) + " KB)");

        String ts = ntpMgr.getTimeStr();
        logger.add(LOG_OTA,
            ("OTA " + typeLabel + " start: " + _otaFileName).c_str(),
            ts.length() ? ts.c_str() : nullptr);

        bool ok = _downloadAndFlash(url, _otaIsFirmware ? U_FLASH : U_SPIFFS);

        if (ok) {
            logger.add(LOG_OTA,
                ("OTA " + typeLabel + " sukses! Restart...").c_str(),
                ts.length() ? ts.c_str() : nullptr);
            sendNotif("✅ *OTA " + typeLabel + " Berhasil!*\n"
                      "File `" + _otaFileName + "` ter-flash.\n"
                      "Restart dalam 3 detik...");
            delay(3000);
            ESP.restart();
        } else {
            logger.add(LOG_OTA,
                ("OTA " + typeLabel + " gagal: " + String(Update.errorString())).c_str(),
                nullptr);
            sendNotif("❌ *OTA " + typeLabel + " Gagal*\n"
                      "Error: `" + String(Update.errorString()) + "`\n"
                      "Perangkat tetap berjalan dengan " + typeLabel + " lama.");
            _otaState    = OTA_IDLE;
            _otaFilePath = "";
        }
    }

private:
    // ── Proses setiap pesan masuk ─────────────────────────────
    void _processMessage(telegramMessage& msg) {
        String fromId = msg.chat_id;
        String text   = msg.text;

        // Whitelist chat_id
        if (!_chatId.isEmpty() && fromId != _chatId) {
            _bot->sendMessage(fromId, "❌ Akses ditolak.", "");
            return;
        }

        String ts = ntpMgr.getTimeStr();

        // ── Handle file document (OTA firmware) ───────────────
        // Library mengisi msg.hasDocument=true, msg.file_path (URL path CDN),
        // msg.file_name, msg.file_size, msg.file_caption saat user kirim file
        if (msg.hasDocument) {
            String caption = msg.file_caption;
            caption.toLowerCase();
            caption.trim();

            if (caption == "update firmware" || caption == "update spiffs") {
                bool isFirmware = (caption == "update firmware");

                _otaFilePath  = msg.file_path;
                _otaFileName  = msg.file_name;
                _otaFileSize  = msg.file_size;
                _otaIsFirmware = isFirmware;
                _otaState     = OTA_CONFIRM;
                _otaConfirmAt = millis();

                char buf[320];
                snprintf(buf, sizeof(buf),
                    "📦 *%s Diterima*\n"
                    "Nama: `%s`\n"
                    "Ukuran: `%d bytes` (%.1f KB)\n"
                    "Tipe: `%s`\n\n"
                    "Ketik `/ota confirm` untuk mulai flash\n"
                    "Ketik `/ota cancel` untuk batal\n"
                    "_Timeout konfirmasi: 60 detik_",
                    isFirmware ? "File Firmware" : "File Filesystem",
                    _otaFileName.c_str(),
                    _otaFileSize,
                    _otaFileSize / 1024.0f,
                    isFirmware ? "U_FLASH (firmware)" : "U_SPIFFS (SPIFFS)");
                _bot->sendMessage(fromId, String(buf), "Markdown");

                logger.add(LOG_OTA,
                    ((isFirmware ? "FW" : "FS") + String(" diterima: ") +
                     _otaFileName + " (" + String(_otaFileSize) + " bytes)").c_str(),
                    ts.length() ? ts.c_str() : nullptr);
            } else {
                _bot->sendMessage(fromId,
                    "📄 File diterima, caption tidak dikenal.\n\n"
                    "Untuk OTA firmware:\n"
                    "  Kirim `.bin` dengan caption `update firmware`\n\n"
                    "Untuk OTA filesystem (HTML):\n"
                    "  Kirim `.bin` dengan caption `update spiffs`",
                    "Markdown");
            }
            return;
        }

        // ── Command text ──────────────────────────────────────
        logger.add(LOG_TELE, ("CMD: " + text).c_str(),
                   ts.length() ? ts.c_str() : nullptr);

        if      (text == "/start" || text == "/help") _cmdHelp(fromId);
        else if (text == "/status")                    _cmdStatus(fromId);
        else if (text == "/baterai")                   _cmdBatt(fromId);
        else if (text == "/cuaca")                     _cmdEnv(fromId);
        else if (text == "/gempa")                     _cmdSeismic(fromId);
        else if (text == "/alarm off")                 _cmdAlarmOff(fromId);
        else if (text == "/reboot")                    _cmdReboot(fromId);
        else if (text == "/log")                       _cmdLog(fromId);
        else if (text.startsWith("/debug"))            _cmdDebug(fromId, text);
        else if (text.startsWith("/ota"))              _cmdOta(fromId, text);
        else if (text.startsWith("/relay"))            _cmdRelay(fromId, text);
        else {
            _bot->sendMessage(fromId,
                "❓ Command tidak dikenal. Ketik /help", "");
        }
    }

    // ── Download dari CDN Telegram dan flash ──────────────────
    bool _downloadAndFlash(const String& url, int updateType = U_FLASH) {
        HTTPClient http;
        http.begin(*_client, url);
        http.setTimeout(60000);

        int code = http.GET();
        if (code != 200) {
            logger.debug("OTA", "HTTP error: %d", code);
            http.end();
            return false;
        }

        int totalSize = http.getSize();
        if (totalSize <= 0) totalSize = _otaFileSize;
        logger.debug("OTA", "Size: %d bytes type:%s",
            totalSize, updateType == U_FLASH ? "FLASH" : "SPIFFS");

        // Jika filesystem, hentikan SPIFFS dulu
        if (updateType == U_SPIFFS) LittleFS.end();

        if (!Update.begin(totalSize > 0 ? totalSize : UPDATE_SIZE_UNKNOWN, updateType)) {
            logger.debug("OTA", "begin error: %s", Update.errorString());
            http.end();
            return false;
        }

        WiFiClient* stream = http.getStreamPtr();
        uint8_t buf[1024];
        int written = 0;
        unsigned long lastLog = 0;
        int lastPct = -1;

        while (http.connected() && (totalSize <= 0 || written < totalSize)) {
            int avail = stream->available();
            if (avail > 0) {
                int toRead = min(avail, (int)sizeof(buf));
                int rd = stream->readBytes(buf, toRead);
                if (rd <= 0) break;

                if (Update.write(buf, rd) != (size_t)rd) {
                    logger.debug("OTA", "write error: %s", Update.errorString());
                    Update.abort();
                    http.end();
                    return false;
                }
                written += rd;

                // Log progress tiap ~25% atau tiap 3 detik
                if (totalSize > 0 && millis() - lastLog > 3000) {
                    int pct = (written * 100) / totalSize;
                    if (pct != lastPct) {
                        char prog[60];
                        snprintf(prog, sizeof(prog),
                            "OTA progress: %d%% (%d/%d bytes)", pct, written, totalSize);
                        logger.add(LOG_OTA, prog, nullptr);
                        lastPct = pct;
                    }
                    lastLog = millis();
                }
            } else if (!http.connected()) {
                break;
            }
            delay(1);
        }

        http.end();

        if (written == 0) {
            logger.debug("OTA", "Tidak ada data ditulis");
            return false;
        }

        if (!Update.end(true)) {
            logger.debug("OTA", "end error: %s", Update.errorString());
            return false;
        }

        logger.debug("OTA", "Flash selesai: %d bytes ditulis", written);
        return true;
    }

    // ── Commands ──────────────────────────────────────────────
    void _cmdHelp(const String& chatId) {
        String msg =
            "📋 *BattAnalyzer Commands*\n\n"
            "*Info:*\n"
            "/status — Ringkasan sistem\n"
            "/baterai — Detail baterai\n"
            "/cuaca — Kondisi lingkungan\n"
            "/gempa — Status seismik\n"
            "/log — 10 log terakhir\n\n"
            "*Kontrol:*\n"
            "/relay 1-4 on|off — Kontrol relay\n"
            "/alarm off — Matikan buzzer\n"
            "/reboot — Restart perangkat\n\n"
            "*OTA Firmware:*\n"
            "Kirim `firmware.bin` caption `update firmware`\n"
            "Kirim `littlefs.bin` caption `update spiffs`\n"
            "/ota confirm — Konfirmasi flash\n"
            "/ota cancel — Batalkan OTA\n"
            "/ota rollback — Kembali firmware lama\n"
            "/ota status — Cek status OTA\n\n"
            "*Debug:*\n"
            "/debug — Status log grup\n"
            "/debug on|off GRUP — Toggle grup log";
        _bot->sendMessage(chatId, msg, "Markdown");
    }

    void _cmdStatus(const String& chatId) {
        char buf[450];
        unsigned long uptime = millis() / 1000;
        snprintf(buf, sizeof(buf),
            "📊 *Status Sistem*\n\n"
            "🔋 `%.3fV` `%+.3fA` `%.2fW`\n"
            "📶 SoC: `%.0f%%`  SoH: `%.0f%%`\n"
            "Status: `%s`\n"
            "🌡️ `%.1f°C`  💧`%.0f%%RH`\n"
            "📡 MQTT: `%s`  NTP: `%s`\n"
            "🌐 IP: `%s`\n"
            "⏱️ Uptime: `%luh %02lum`\n"
            "💾 RAM: `%lu KB` bebas",
            battMon.voltage, battMon.current, battMon.power,
            battMon.soc, battMon.soh,
            battMon.status,
            envSensor.temperature, envSensor.humidity,
            mqttMgr.isConnected ? "✅" : "❌",
            ntpMgr.synced ? "✅" : "❌",
            WiFi.localIP().toString().c_str(),
            uptime / 3600, (uptime % 3600) / 60,
            (unsigned long)(ESP.getFreeHeap() / 1024));
        _bot->sendMessage(chatId, String(buf), "Markdown");
    }

    void _cmdBatt(const String& chatId) {
        char buf[400];
        snprintf(buf, sizeof(buf),
            "🔋 *Detail Baterai*\n\n"
            "Tegangan: `%.3fV`\n"
            "Arus: `%+.3fA`\n"
            "Daya: `%.2fW`\n"
            "SoC: `%.0f%%`  SoH: `%.0f%%`\n"
            "Status: `%s`\n"
            "R Internal: `%.1f mΩ`\n"
            "C-Rate: `%.2fC`\n"
            "Charge: `%.0f mAh` / `%.2f Wh`\n"
            "Discharge: `%.0f mAh` / `%.2f Wh`\n"
            "Siklus: `%d`",
            battMon.voltage, battMon.current, battMon.power,
            battMon.soc, battMon.soh,
            battMon.status,
            battMon.internalR,
            battMon.cRate,
            battMon.mahCharge, battMon.whCharge,
            battMon.mahDischarge, battMon.whDischarge,
            battMon.cycleCount);
        _bot->sendMessage(chatId, String(buf), "Markdown");
    }

    void _cmdEnv(const String& chatId) {
        char buf[220];
        snprintf(buf, sizeof(buf),
            "🌡️ *Lingkungan*\n\n"
            "Suhu: `%.1f°C`\n"
            "Kelembaban: `%.1f%%`\n"
            "Tekanan: `%.1f hPa`\n"
            "Ketinggian: `%.0f m`\n"
            "Cuaca: %s `%s`",
            envSensor.temperature, envSensor.humidity,
            envSensor.pressure, envSensor.altitude,
            envSensor.weatherIcon, envSensor.weatherText);
        _bot->sendMessage(chatId, String(buf), "Markdown");
    }

    void _cmdSeismic(const String& chatId) {
        char buf[220];
        snprintf(buf, sizeof(buf),
            "🌍 *Seismik*\n\n"
            "Status: `%s`\n"
            "Magnitude: `%.4fg`\n"
            "AccX: `%.3f`  AccY: `%.3f`  AccZ: `%.3f`\n"
            "Kalibrasi: %s",
            seismic.statusText(), seismic.magnitude,
            seismic.accX, seismic.accY, seismic.accZ,
            seismic.isCalibrated ? "✅" : "❌");
        _bot->sendMessage(chatId, String(buf), "Markdown");
    }

    void _cmdAlarmOff(const String& chatId) {
        buzzer.stop();
        _bot->sendMessage(chatId, "🔕 Alarm dimatikan.", "");
        logger.add(LOG_TELE, "Alarm dimatikan via Telegram", nullptr);
    }

    void _cmdReboot(const String& chatId) {
        _bot->sendMessage(chatId, "🔄 Restart dalam 3 detik...", "");
        logger.add(LOG_TELE, "Reboot via Telegram", nullptr);
        delay(3000);
        ESP.restart();
    }

    void _cmdLog(const String& chatId) {
        String out = "📋 *10 Log Terakhir*\n\n```\n";
        int shown = 0;
        for (int i = 0; i < LOG_MAX_ENTRIES && shown < 10; i++) {
            int idx = (logger.head - 1 - i + LOG_MAX_ENTRIES) % LOG_MAX_ENTRIES;
            LogEntry& e = logger.entries[idx];
            if (!e.valid) continue;
            char line[100];
            snprintf(line, sizeof(line), "[%s] %s\n%.60s\n",
                e.time, e.cat, e.msg);
            out += line;
            shown++;
        }
        out += "```";
        _bot->sendMessage(chatId, out, "Markdown");
    }

    void _cmdDebug(const String& chatId, const String& text) {
        if (text == "/debug" || text == "/debug status") {
            uint32_t m = logger.mask;
            char buf[300];
            snprintf(buf, sizeof(buf),
                "🔍 *Status Log Grup*\n\n"
                "SYSTEM: %s  WIFI: %s\n"
                "MQTT:   %s  SENSOR: %s\n"
                "BATT:   %s  ALERT: %s\n"
                "TELE:   %s  OTA: %s\n"
                "DEBUG:  %s\n\n"
                "_/debug on|off GRUP_",
                (m & LOG_MASK_SYSTEM) ? "✅" : "❌",
                (m & LOG_MASK_WIFI)   ? "✅" : "❌",
                (m & LOG_MASK_MQTT)   ? "✅" : "❌",
                (m & LOG_MASK_SENSOR) ? "✅" : "❌",
                (m & LOG_MASK_BATT)   ? "✅" : "❌",
                (m & LOG_MASK_ALERT)  ? "✅" : "❌",
                (m & LOG_MASK_TELE)   ? "✅" : "❌",
                (m & LOG_MASK_OTA)    ? "✅" : "❌",
                (m & LOG_MASK_DEBUG)  ? "✅" : "❌");
            _bot->sendMessage(chatId, String(buf), "Markdown");
            return;
        }

        if (text == "/debug all") {
            logger.setMask(LOG_MASK_ALL); nvs.setLogMask(LOG_MASK_ALL);
            _bot->sendMessage(chatId, "✅ Semua grup log diaktifkan.", "");
            return;
        }
        if (text == "/debug none") {
            logger.setMask(0); nvs.setLogMask(0);
            _bot->sendMessage(chatId, "⛔ Semua grup log dinonaktifkan.", "");
            return;
        }

        char action[8] = "", group[12] = "";
        if (sscanf(text.c_str(), "/debug %7s %11s", action, group) == 2) {
            bool enable = (strcmp(action, "on") == 0);
            uint32_t bit = _groupToBit(group);
            if (bit == 0) {
                _bot->sendMessage(chatId,
                    "❓ Grup tidak dikenal.\n"
                    "Pilihan: SYSTEM WIFI MQTT SENSOR BATT ALERT TELE OTA DEBUG", "");
                return;
            }
            uint32_t m = logger.mask;
            if (enable) m |= bit; else m &= ~bit;
            logger.setMask(m); nvs.setLogMask(m);
            char rsp[80];
            snprintf(rsp, sizeof(rsp), "%s Grup `%s` %s",
                enable ? "✅" : "⛔", group, enable ? "diaktifkan" : "dinonaktifkan");
            _bot->sendMessage(chatId, String(rsp), "Markdown");
        } else {
            _bot->sendMessage(chatId,
                "Format: `/debug on GRUP` atau `/debug off GRUP`\n"
                "Contoh: `/debug on MQTT`", "Markdown");
        }
    }

    void _cmdOta(const String& chatId, const String& text) {
        if (text == "/ota confirm") {
            if (_otaState != OTA_CONFIRM || _otaFilePath.isEmpty()) {
                _bot->sendMessage(chatId,
                    "❓ Tidak ada firmware menunggu konfirmasi.\n\n"
                    "Kirim file `.bin` ke chat ini dengan caption:\n"
                    "`update firmware`", "Markdown");
                return;
            }
            _otaState = OTA_DOWNLOAD;
            _bot->sendMessage(chatId,
                "⚙️ OTA dimulai...\n"
                "Proses 30-60 detik, jangan matikan perangkat!", "");
            // processOta() dipanggil dari main loop()

        } else if (text == "/ota cancel") {
            _otaState    = OTA_IDLE;
            _otaFilePath = "";
            _bot->sendMessage(chatId, "🚫 OTA dibatalkan.", "");

        } else if (text == "/ota rollback") {
            if (Update.canRollBack()) {
                _bot->sendMessage(chatId,
                    "⏪ Rollback ke firmware sebelumnya...\nRestart dalam 3 detik.", "");
                delay(3000);
                Update.rollBack();
                ESP.restart();
            } else {
                _bot->sendMessage(chatId,
                    "❌ Tidak ada firmware lama yang bisa di-rollback.", "");
            }

        } else if (text == "/ota status") {
            const char* states[] = {
                "Idle", "Menunggu konfirmasi", "Siap download", "Sedang flash"
            };
            String rsp = "📦 *OTA Status*: `";
            rsp += states[(int)_otaState];
            rsp += "`";
            if (_otaState == OTA_CONFIRM || _otaState == OTA_FLASHING) {
                rsp += "\nFile: `" + _otaFileName + "`";
                rsp += "\nUkuran: `" + String(_otaFileSize) + " bytes`";
                rsp += "\nTipe: `" + String(_otaIsFirmware ? "firmware (U_FLASH)" : "filesystem (U_SPIFFS)") + "`";
            }
            _bot->sendMessage(chatId, rsp, "Markdown");

        } else {
            _bot->sendMessage(chatId,
                "📦 *OTA Firmware*\n\n"
                "1. Kirim `firmware.bin` caption `update firmware`\n"
                "   atau `littlefs.bin` caption `update spiffs`\n"
                "2. `/ota confirm` — mulai flash\n"
                "3. `/ota cancel` — batal\n"
                "4. `/ota rollback` — kembali firmware lama\n"
                "5. `/ota status` — cek status",
                "Markdown");
        }
    }

    void _cmdRelay(const String& chatId, const String& text) {
        int idx; char cmd[8];
        if (sscanf(text.c_str(), "/relay %d %7s", &idx, cmd) == 2) {
            if (idx >= 1 && idx <= RELAY_COUNT) {
                bool on = (strcmp(cmd, "on") == 0 || strcmp(cmd, "ON") == 0);
                relayMgr.set(idx - 1, on);
                String rsp = "✅ Relay " + String(idx) + " `" + (on ? "ON" : "OFF") + "`";
                _bot->sendMessage(chatId, rsp, "Markdown");
                String ts = ntpMgr.getTimeStr();
                char logbuf[60];
                snprintf(logbuf, sizeof(logbuf),
                    "Relay %d → %s via Telegram", idx, on ? "ON" : "OFF");
                logger.add(LOG_TELE, logbuf, ts.length() ? ts.c_str() : nullptr);
            } else {
                _bot->sendMessage(chatId, "❌ Nomor relay 1-4.", "");
            }
        } else {
            _bot->sendMessage(chatId,
                "Format: `/relay N on` atau `/relay N off`\n"
                "Contoh: `/relay 2 on`", "Markdown");
        }
    }

    uint32_t _groupToBit(const char* g) {
        if (strcmp(g, "SYSTEM") == 0) return LOG_MASK_SYSTEM;
        if (strcmp(g, "WIFI")   == 0) return LOG_MASK_WIFI;
        if (strcmp(g, "MQTT")   == 0) return LOG_MASK_MQTT;
        if (strcmp(g, "SENSOR") == 0) return LOG_MASK_SENSOR;
        if (strcmp(g, "BATT")   == 0) return LOG_MASK_BATT;
        if (strcmp(g, "ALERT")  == 0) return LOG_MASK_ALERT;
        if (strcmp(g, "TELE")   == 0) return LOG_MASK_TELE;
        if (strcmp(g, "OTA")    == 0) return LOG_MASK_OTA;
        if (strcmp(g, "DEBUG")  == 0) return LOG_MASK_DEBUG;
        return 0;
    }
};

TelegramBot teleBot;