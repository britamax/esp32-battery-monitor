#pragma once

// ============================================================
// BattAnalyzer — Logger.h
// Ring buffer 200 entri — push via WebSocket
// Per-grup on/off via bitmask (NVS key: log_mask)
// Kategori: SYSTEM, WIFI, MQTT, SENSOR, BATT, ALERT, TELE, OTA, DEBUG
// ============================================================

#include <Arduino.h>
#include "Config.h"

struct LogEntry {
    char time[22];         // "DD Mon YYYY HH:MM:SS" atau "+Xs"
    char cat[8];           // kategori
    char msg[LOG_MSG_LEN];
    bool valid;
};

// Callback WebSocket push — diset oleh WebServer setelah init
typedef void (*WsPushFn)(const String&);
static WsPushFn _wsPushCb = nullptr;
inline void setWsPushCallback(WsPushFn fn) { _wsPushCb = fn; }

class Logger {
public:
    LogEntry entries[LOG_MAX_ENTRIES] = {};
    int      head  = 0;
    int      count = 0;

    // Mask aktif — dibaca dari NVS saat begin(), bisa diubah runtime
    uint32_t mask = LOG_MASK_DEFAULT;

    void begin() {
        // Akan dibaca dari NVS di main.cpp setelah nvs.begin()
        // Dipanggil manual: logger.setMask(nvs.getLogMask())
    }

    void setMask(uint32_t m) {
        mask = m;
    }

    // ── Helper: map kategori string ke bit mask ────────────────
    static uint32_t catToBit(const char* cat) {
        if (strcmp(cat, LOG_SYSTEM) == 0) return LOG_MASK_SYSTEM;
        if (strcmp(cat, LOG_WIFI)   == 0) return LOG_MASK_WIFI;
        if (strcmp(cat, LOG_MQTT)   == 0) return LOG_MASK_MQTT;
        if (strcmp(cat, LOG_SENSOR) == 0) return LOG_MASK_SENSOR;
        if (strcmp(cat, LOG_BATT)   == 0) return LOG_MASK_BATT;
        if (strcmp(cat, LOG_ALERT)  == 0) return LOG_MASK_ALERT;
        if (strcmp(cat, LOG_TELE)   == 0) return LOG_MASK_TELE;
        if (strcmp(cat, LOG_OTA)    == 0) return LOG_MASK_OTA;
        if (strcmp(cat, LOG_DEBUG)  == 0) return LOG_MASK_DEBUG;
        return LOG_MASK_SYSTEM;  // default: selalu tampil
    }

    // ── Tambah log entry ──────────────────────────────────────
    void add(const char* cat, const char* msg, const char* timeStr = nullptr) {
        // Cek mask — kalau grup ini off, skip simpan ke buffer
        // tapi tetap print ke Serial untuk debugging via USB
        bool allowed = (mask & catToBit(cat)) != 0;

        // Selalu print ke Serial (untuk debug via USB jika tersambung)
        Serial.printf("[%s] %s\n", cat, msg);

        if (!allowed) return;  // tidak simpan ke buffer/WS

        LogEntry& e = entries[head];
        e.valid = true;

        if (timeStr && strlen(timeStr) > 0) {
            strncpy(e.time, timeStr, sizeof(e.time) - 1);
            e.time[sizeof(e.time) - 1] = '\0';
        } else {
            unsigned long s = millis() / 1000;
            snprintf(e.time, sizeof(e.time), "+%lus", s);
        }

        strncpy(e.cat, cat, sizeof(e.cat) - 1);
        e.cat[sizeof(e.cat) - 1] = '\0';
        strncpy(e.msg, msg, sizeof(e.msg) - 1);
        e.msg[sizeof(e.msg) - 1] = '\0';

        // Push ke WebSocket
        if (_wsPushCb) {
            String j = "{\"type\":\"log\",\"t\":\"";
            j += e.time; j += "\",\"c\":\""; j += e.cat;
            j += "\",\"m\":\"";
            String m2 = e.msg;
            m2.replace("\\", "\\\\"); m2.replace("\"", "\\\"");
            j += m2; j += "\"}";
            _wsPushCb(j);
        }

        head = (head + 1) % LOG_MAX_ENTRIES;
        if (count < LOG_MAX_ENTRIES) count++;
    }

    // ── Format printf ──────────────────────────────────────────
    void addf(const char* cat, const char* timeStr, const char* fmt, ...) {
        char buf[LOG_MSG_LEN];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        add(cat, buf, timeStr);
    }

    // ── Shorthand debug — hanya simpan jika DEBUG mask aktif ──
    void debug(const char* module, const char* fmt, ...) {
        if (!(mask & LOG_MASK_DEBUG)) {
            // Tetap print serial
            Serial.printf("[DEBUG/%s] ", module);
            va_list args; va_start(args, fmt);
            char buf[LOG_MSG_LEN]; vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
            Serial.println(buf);
            return;
        }
        char buf[LOG_MSG_LEN];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        // Prefix module name
        char full[LOG_MSG_LEN];
        snprintf(full, sizeof(full), "[%s] %s", module, buf);
        add(LOG_DEBUG, full, nullptr);
    }

    void clear() {
        for (int i = 0; i < LOG_MAX_ENTRIES; i++) entries[i].valid = false;
        head = 0; count = 0;
    }

    // ── Serialize ke JSON untuk /api/log ──────────────────────
    String toJson(const char* filterCat = nullptr) {
        String out = "[";
        bool first = true;
        for (int i = 0; i < LOG_MAX_ENTRIES; i++) {
            int idx = (head - 1 - i + LOG_MAX_ENTRIES) % LOG_MAX_ENTRIES;
            LogEntry& e = entries[idx];
            if (!e.valid) continue;
            if (filterCat && strlen(filterCat) > 0 && strcmp(filterCat, "all") != 0) {
                if (strcmp(e.cat, filterCat) != 0) continue;
            }
            if (!first) out += ",";
            first = false;
            String m = e.msg;
            m.replace("\\", "\\\\"); m.replace("\"", "\\\"");
            out += "{\"t\":\""; out += e.time;
            out += "\",\"c\":\""; out += e.cat;
            out += "\",\"m\":\""; out += m; out += "\"}";
        }
        out += "]";
        return out;
    }

    // ── Status mask sebagai JSON untuk UI ─────────────────────
    String maskToJson() {
        String j = "{";
        j += "\"SYSTEM\":"  + String((mask & LOG_MASK_SYSTEM) ? "true" : "false") + ",";
        j += "\"WIFI\":"    + String((mask & LOG_MASK_WIFI)   ? "true" : "false") + ",";
        j += "\"MQTT\":"    + String((mask & LOG_MASK_MQTT)   ? "true" : "false") + ",";
        j += "\"SENSOR\":"  + String((mask & LOG_MASK_SENSOR) ? "true" : "false") + ",";
        j += "\"BATT\":"    + String((mask & LOG_MASK_BATT)   ? "true" : "false") + ",";
        j += "\"ALERT\":"   + String((mask & LOG_MASK_ALERT)  ? "true" : "false") + ",";
        j += "\"TELE\":"    + String((mask & LOG_MASK_TELE)   ? "true" : "false") + ",";
        j += "\"OTA\":"     + String((mask & LOG_MASK_OTA)    ? "true" : "false") + ",";
        j += "\"DEBUG\":"   + String((mask & LOG_MASK_DEBUG)  ? "true" : "false");
        j += "}";
        return j;
    }
};

Logger logger;