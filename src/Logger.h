#pragma once

// ============================================================
// BattAnalyzer — Logger.h
// Ring buffer 200 entri — push via WebSocket
// Kategori: SYSTEM, WIFI, MQTT, SENSOR, BATT, ALERT, TELE, OTA
// ============================================================

#include <Arduino.h>
#include "Config.h"

struct LogEntry {
    char time[22];        // "DD Mon YYYY HH:MM:SS"
    char cat[8];          // kategori
    char msg[LOG_MSG_LEN];
    bool valid;
};

// Callback WebSocket push — diset oleh WebServer setelah init
// Pattern ini menghindari circular include antara Logger.h ↔ WebServer.h
typedef void (*WsPushFn)(const String&);
static WsPushFn _wsPushCb = nullptr;

inline void setWsPushCallback(WsPushFn fn) { _wsPushCb = fn; }

class Logger {
public:
    LogEntry entries[LOG_MAX_ENTRIES] = {};
    int      head  = 0;
    int      count = 0;

    // Tambah log entry
    void add(const char* cat, const char* msg, const char* timeStr = nullptr) {
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

        // Serial output
        Serial.printf("[%s] %s\n", cat, msg);

        // WebSocket push via callback (jika WebServer sudah inisialisasi)
        if (_wsPushCb) {
            String j = "{\"type\":\"log\",\"t\":\"";
            j += e.time; j += "\",\"c\":\""; j += e.cat;
            j += "\",\"m\":\"";
            String m2 = e.msg;
            m2.replace("\\", "\\\\"); m2.replace("\"", "\\\"");
            j += m2; j += "\"}";
            _wsPushCb(j);
        }

        // Circular buffer advance
        head = (head + 1) % LOG_MAX_ENTRIES;
        if (count < LOG_MAX_ENTRIES) count++;
    }

    // Format printf
    void addf(const char* cat, const char* timeStr, const char* fmt, ...) {
        char buf[LOG_MSG_LEN];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        add(cat, buf, timeStr);
    }

    void clear() {
        for (int i = 0; i < LOG_MAX_ENTRIES; i++) entries[i].valid = false;
        head = 0; count = 0;
    }

    // Serialize ke JSON array untuk /api/log
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
};

Logger logger;
