#pragma once

// ============================================================
// BattAnalyzer — SessionManager.h
// Session token — RAM only, 7 hari expired, max 4 sesi
// ============================================================

#include <Arduino.h>
#include <ESPAsyncWebServer.h> 

#define SESSION_DURATION_MS  (7UL * 24 * 3600 * 1000)
#define SESSION_TOKEN_LEN    32
#define MAX_SESSIONS         4

struct Session {
    char          token[SESSION_TOKEN_LEN + 1];
    unsigned long createdAt;
    bool          valid;
};

class SessionManager {
public:
    Session sessions[MAX_SESSIONS] = {};

    String createSession() {
        String token = "";
        for (int i = 0; i < SESSION_TOKEN_LEN; i++)
            token += String(random(0, 16), HEX);
        int slot = _findSlot();
        strncpy(sessions[slot].token, token.c_str(), SESSION_TOKEN_LEN);
        sessions[slot].token[SESSION_TOKEN_LEN] = '\0';
        sessions[slot].createdAt = millis();
        sessions[slot].valid     = true;
        return token;
    }

    bool isValid(AsyncWebServerRequest* req) {
        String t = extractToken(req);
        if (t.isEmpty()) return false;
        return validateToken(t);
    }

    bool validateToken(const String& token) {
        unsigned long now = millis();
        for (int i = 0; i < MAX_SESSIONS; i++) {
            if (!sessions[i].valid) continue;
            if (now - sessions[i].createdAt > SESSION_DURATION_MS) {
                sessions[i].valid = false; continue;
            }
            if (token == sessions[i].token) return true;
        }
        return false;
    }

    void invalidateToken(const String& token) {
        for (int i = 0; i < MAX_SESSIONS; i++)
            if (sessions[i].valid && token == sessions[i].token)
                { sessions[i].valid = false; memset(sessions[i].token, 0, sizeof(sessions[i].token)); }
    }

    void invalidateAll() {
        for (int i = 0; i < MAX_SESSIONS; i++)
            { sessions[i].valid = false; memset(sessions[i].token, 0, sizeof(sessions[i].token)); }
    }

    String extractToken(AsyncWebServerRequest* req) {
        if (!req->hasHeader("Cookie")) return "";
        String c = req->header("Cookie");
        int idx = c.indexOf("ba_session=");
        if (idx < 0) return "";
        int start = idx + 11;
        int end   = c.indexOf(';', start);
        if (end < 0) end = c.length();
        return c.substring(start, end);
    }

    static String makeCookie(const String& token) {
        return "ba_session=" + token + "; Path=/; HttpOnly; Max-Age=604800; SameSite=Lax";
    }
    static String clearCookie() {
        return "ba_session=; Path=/; HttpOnly; Max-Age=0";
    }

private:
    int _findSlot() {
        for (int i = 0; i < MAX_SESSIONS; i++)
            if (!sessions[i].valid) return i;
        int oldest = 0;
        unsigned long minT = sessions[0].createdAt;
        for (int i = 1; i < MAX_SESSIONS; i++)
            if (sessions[i].createdAt < minT) { minT = sessions[i].createdAt; oldest = i; }
        return oldest;
    }
};

SessionManager sessionMgr;
