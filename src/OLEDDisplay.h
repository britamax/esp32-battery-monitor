#pragma once

// ============================================================
// BattAnalyzer — OLEDDisplay.h
// OLED SSD1306 128×32 — 7 halaman info
// Hal.0: WiFi Status | Hal.1: Baterai | Hal.2: Power
// Hal.3: Akumulasi   | Hal.4: Env     | Hal.5: Seismic | Hal.6: Sistem
// ============================================================

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Config.h"

class OLEDDisplay {
public:
    bool isOnline    = false;
    bool enabled     = true;
    bool autoScroll  = false;
    int  currentPage = 0;
    int  pageDurMs   = 3000;

    // Data dari modul lain (di-set dari main.cpp)
    struct {
        String mode, ssid, ip;
        bool   wifiOk = false;
        int    rssi   = 0;
    } wifi;

    struct {
        float  voltage, current, soc;
        const char* status = "IDLE";
    } batt;

    struct {
        float  whChg, whDis;
        float  mahChg, mahDis;
        int    cycles;
    } energy;

    struct {
        float  temp, humidity, pressure;
        const char* weather = "---";
    } env;

    struct {
        float  magnitude;
        const char* status = "AMAN";
    } seismic;

    struct {
        unsigned long uptime = 0;
        String version;
        bool   mqttOk = false;
    } sys;

private:
    Adafruit_SSD1306 _oled;
    unsigned long _lastPage = 0;

public:
    OLEDDisplay() : _oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1) {}

    bool begin() {
        isOnline = _oled.begin(SSD1306_SWITCHCAPVCC, ADDR_OLED);
        if (isOnline) {
            _oled.clearDisplay();
            _oled.display();
        }
        return isOnline;
    }

    void showSplash(const String& devName) {
        if (!isOnline) return;
        _oled.clearDisplay();
        _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _oled.setCursor(0,0);  _oled.println("** BattAnalyzer **");
        _oled.setCursor(0,11); _oled.println(devName);
        _oled.setCursor(0,23); _oled.print("fw v"); _oled.println(FW_VERSION);
        _oled.display();
        delay(1500);
    }

    void showAPMode() {
        if (!isOnline) return;
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _oled.setCursor(0,0);  _oled.println("-- SETUP MODE --");
        _oled.setCursor(0,11); _oled.println(AP_SSID);
        _oled.setCursor(0,23); _oled.println(AP_IP_STR);
        _oled.display();
    }

    void showConnecting(const String& ssid, int attempt) {
        if (!isOnline) return;
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _oled.setCursor(0,0);  _oled.println("Konek WiFi...");
        _oled.setCursor(0,11); _oled.println(ssid.substring(0,20));
        _oled.setCursor(0,23);
        for (int i = 0; i < (attempt % 12) + 1; i++) _oled.print(".");
        _oled.display();
    }

    void showStatus(const String& l1, const String& l2, const String& l3) {
        if (!isOnline) return;
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _oled.setCursor(0,0);  _oled.println(l1);
        _oled.setCursor(0,11); _oled.println(l2);
        _oled.setCursor(0,23); _oled.println(l3);
        _oled.display();
    }

    void setPage(int p) {
        if (p >= 0 && p < OLED_PAGE_COUNT) { currentPage = p; _lastPage = millis(); }
    }

    void nextPage() {
        currentPage = (currentPage + 1) % OLED_PAGE_COUNT;
        _lastPage = millis();
    }

    void setBrightness(int v) {
        if (!isOnline) return;
        v = constrain(v, 0, 255);
        _oled.ssd1306_command(SSD1306_SETCONTRAST);
        _oled.ssd1306_command(v);
    }

    void setEnabled(bool on) {
        if (!isOnline) return;
        enabled = on;
        _oled.ssd1306_command(on ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
    }

    // Dipanggil dari loop() tiap siklus
    void update() {
        if (!isOnline || !enabled) return;
        if (autoScroll && millis() - _lastPage >= (unsigned long)pageDurMs) {
            currentPage = (currentPage + 1) % OLED_PAGE_COUNT;
            _lastPage = millis();
        }
        switch (currentPage) {
            case 0: _pageWifi();    break;
            case 1: _pageBatt();    break;
            case 2: _pagePower();   break;
            case 3: _pageEnergy();  break;
            case 4: _pageEnv();     break;
            case 5: _pageSeismic(); break;
            case 6: _pageSys();     break;
            default: _pageWifi();   break;
        }
    }

private:
    // ── Hal.0: WiFi Status ──────────────────────────────────────
    void _pageWifi() {
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _oled.setCursor(0,0);
        if (wifi.wifiOk)
            _oled.printf("%-4s %s", wifi.mode.c_str(), wifi.ssid.substring(0,12).c_str());
        else
            _oled.printf("%-4s --", wifi.mode.c_str());
        _oled.setCursor(0,11);
        _oled.printf("IP: %s", wifi.wifiOk ? wifi.ip.c_str() : "-");
        _oled.setCursor(0,23);
        _oled.printf("RSSI:%ddBm BL:%d", wifi.rssi, (int)batt.soc);
        _oled.print("%");
        _oled.display();
    }

    // ── Hal.1: Baterai ──────────────────────────────────────────
    void _pageBatt() {
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _oled.setCursor(0,0);  _oled.printf("-BATERAI- %s", batt.status);
        _oled.setCursor(0,11); _oled.printf("V:%5.2fV  I:%+5.2fA", batt.voltage, batt.current);
        _oled.setCursor(0,23);
        _oled.printf("SoC:%3d%% [", (int)batt.soc);
        int filled = map((int)batt.soc, 0, 100, 0, 8);
        for (int i = 0; i < 8; i++) _oled.print(i < filled ? "#" : "-");
        _oled.print("]");
        _oled.display();
    }

    // ── Hal.2: Power ────────────────────────────────────────────
    void _pagePower() {
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _oled.setCursor(0,0);  _oled.println("-DAYA-");
        float pIn  = batt.current > 0  ? batt.voltage * batt.current    : 0;
        float pOut = batt.current < 0  ? batt.voltage * (-batt.current) : 0;
        _oled.setCursor(0,11); _oled.printf("IN : %6.2f W", pIn);
        _oled.setCursor(0,23); _oled.printf("OUT: %6.2f W", pOut);
        _oled.display();
    }

    // ── Hal.3: Akumulasi ────────────────────────────────────────
    void _pageEnergy() {
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _oled.setCursor(0,0);  _oled.println("-AKUMULASI-");
        _oled.setCursor(0,11); _oled.printf("CHG:%6.0fmAh DIS:%6.0f", energy.mahChg, energy.mahDis);
        _oled.setCursor(0,23); _oled.printf("Wh:%6.2f  Cyc:%d", energy.whChg - energy.whDis, energy.cycles);
        _oled.display();
    }

    // ── Hal.4: Lingkungan ───────────────────────────────────────
    void _pageEnv() {
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _oled.setCursor(0,0);  _oled.printf("-ENV- %s", env.weather);
        _oled.setCursor(0,11); _oled.printf("T:%.1fC  H:%.1f%%", env.temp, env.humidity);
        _oled.setCursor(0,23); _oled.printf("P:%.1fhPa", env.pressure);
        _oled.display();
    }

    // ── Hal.5: Seismic ──────────────────────────────────────────
    void _pageSeismic() {
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _oled.setCursor(0,0);  _oled.printf("-SEISMIC- %s", seismic.status);
        _oled.setCursor(0,11); _oled.printf("Mag: %.4f g", seismic.magnitude);
        _oled.setCursor(0,23); _oled.println("INT1 Hardware ISR");
        _oled.display();
    }

    // ── Hal.6: Sistem ───────────────────────────────────────────
    void _pageSys() {
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        unsigned long s = sys.uptime;
        _oled.setCursor(0,0);
        _oled.printf("v%s MQTT:%s", sys.version.c_str(), sys.mqttOk ? "OK" : "--");
        _oled.setCursor(0,11);
        _oled.printf("IP: %s", wifi.ip.c_str());
        _oled.setCursor(0,23);
        _oled.printf("Up:%lud %02lu:%02lu", s/86400, (s%86400)/3600, (s%3600)/60);
        _oled.display();
    }
};

OLEDDisplay oled;
