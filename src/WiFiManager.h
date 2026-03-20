#pragma once

// ============================================================
// BattAnalyzer — WiFiManager.h
// Koneksi WiFi STA / AP setup mode / Local AP permanen
// DNS Captive Portal saat AP mode
// ============================================================

#include <WiFi.h>
#include <DNSServer.h>
#include "Config.h"
#include "NVSManager.h"
#include "OLEDDisplay.h"

static DNSServer _dns;

class WiFiManager {
public:
    bool   isConnected  = false;
    bool   isAPMode     = false;
    bool   isSetupMode  = false;
    String ipAddress    = "";
    String ssid         = "";

    void begin() {
        isConnected = isAPMode = isSetupMode = false;
        String mode = nvs.getWifiMode();

        if (mode == "ap") {
            _startLocalAP();
            return;
        }

        // STA mode — perlu credentials
        if (!nvs.hasWifiCreds()) {
            _startSetupAP();
            return;
        }

        _connectSTA(nvs.getWifiSSID(), nvs.getWifiPass());

        if (!isConnected) {
            // Fallback AP setup jika gagal
            _startSetupAP();
        }
    }

    void handle() {
        if (isAPMode) _dns.processNextRequest();
    }

    // Cek reconnect saat STA mode putus
    void checkReconnect() {
        if (isAPMode || isSetupMode) return;
        if (WiFi.status() != WL_CONNECTED) {
            isConnected = false;
            static unsigned long lastAttempt = 0;
            if (millis() - lastAttempt >= 30000UL) {
                lastAttempt = millis();
                Serial.println("[WiFi] Reconnect...");
                WiFi.reconnect();
            }
        } else {
            isConnected = true;
            ipAddress   = WiFi.localIP().toString();
        }
    }

private:
    void _connectSTA(const String& ssidStr, const String& pass) {
        WiFi.setHostname(nvs.getDeviceName().c_str());

        // IP Static
        if (nvs.getIPMode() == "static") {
            IPAddress ip, gw, sn, dns;
            if (ip.fromString(nvs.getStaticIP()) && gw.fromString(nvs.getGateway())) {
                sn.fromString(nvs.getSubnet().length() > 5 ? nvs.getSubnet() : "255.255.255.0");
                dns.fromString(nvs.getDNS().length() > 5 ? nvs.getDNS() : "8.8.8.8");
                WiFi.config(ip, gw, sn, dns);
            }
        }

        // ESP32-C3: full reset WiFi stack sebelum konek
        WiFi.persistent(false);
        WiFi.disconnect(true, true);
        delay(500);
        WiFi.mode(WIFI_STA);
        delay(200);
        WiFi.setTxPower(WIFI_POWER_8_5dBm); // ESP32-C3 Super Mini: turunkan power, antenna kecil

        Serial.printf("[WiFi] SSID: '%s' | pass_len: %d\n",
            ssidStr.c_str(), pass.length());

        // Coba konek sampai 3x
        for (int attempt = 0; attempt < 3; attempt++) {
            if (attempt > 0) {
                Serial.printf("[WiFi] Retry %d/3...\n", attempt + 1);
                WiFi.disconnect(false);
                delay(1000);
            }
            WiFi.begin(ssidStr.c_str(), pass.c_str());

            unsigned long start = millis();
            int dot = 0;
            while (WiFi.status() != WL_CONNECTED) {
                if (millis() - start >= 15000UL) break;
                oled.showConnecting(ssidStr, dot++);
                delay(500);
            }

            if (WiFi.status() == WL_CONNECTED) break;
            Serial.printf("[WiFi] Attempt %d gagal, status: %d\n", attempt+1, WiFi.status());
        }

        if (WiFi.status() != WL_CONNECTED) {
            Serial.printf("[WiFi] Timeout! Status: %d\n", WiFi.status());
            return;
        }

        isConnected = true;
        ssid        = ssidStr;
        ipAddress   = WiFi.localIP().toString();
        Serial.printf("[WiFi] OK! IP: %s RSSI: %d\n", ipAddress.c_str(), WiFi.RSSI());

        oled.wifi.wifiOk = true;
        oled.wifi.mode   = "STA";
        oled.wifi.ssid   = ssidStr;
        oled.wifi.ip     = ipAddress;
        oled.wifi.rssi   = WiFi.RSSI();
    }

    void _startSetupAP() {
        isAPMode   = true;
        isSetupMode= true;
        WiFi.mode(WIFI_AP);
        // Tanpa password — penting agar SSID broadcast bekerja
        WiFi.softAP(AP_SSID);
        WiFi.softAPsetHostname(nvs.getDeviceName().c_str());
        _dns.start(53, "*", WiFi.softAPIP());
        ipAddress = WiFi.softAPIP().toString();

        oled.wifi.mode   = "AP";
        oled.wifi.ssid   = AP_SSID;
        oled.wifi.ip     = ipAddress;
        oled.wifi.wifiOk = true;

        oled.showAPMode();
        Serial.printf("[AP] Setup mode: %s @ %s\n", AP_SSID, ipAddress.c_str());
    }

    void _startLocalAP() {
        isAPMode    = true;
        isSetupMode = false;
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID);
        WiFi.softAPsetHostname(nvs.getDeviceName().c_str());
        _dns.start(53, "*", WiFi.softAPIP());
        ipAddress = WiFi.softAPIP().toString();

        oled.wifi.mode   = "AP";
        oled.wifi.ssid   = AP_SSID;
        oled.wifi.ip     = ipAddress;
        oled.wifi.wifiOk = true;

        Serial.printf("[AP] Local mode: %s @ %s\n", AP_SSID, ipAddress.c_str());
    }
};

WiFiManager wifiMgr;