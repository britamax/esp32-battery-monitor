#pragma once

// ============================================================
// BattAnalyzer — OLEDDisplay.h
// OLED SSD1306 128×64 (upgrade dari 128×32)
//
// Halaman (8 total):
//   0: WiFi + Status    4: Lingkungan
//   1: Baterai utama    5: Seismik
//   2: Power + SoC      6: Sistem + Uptime
//   3: Akumulasi        7: INA Debug
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

    struct { String mode, ssid, ip; bool wifiOk=false; int rssi=0; } wifi;
    struct { float voltage=0, current=0, soc=0, soh=100, internalR=0, cRate=0;
             const char* status="IDLE"; } batt;
    struct { float mahChg=0, mahDis=0, whChg=0, whDis=0; int cycles=0; } energy;
    struct { float temp=0, humidity=0, pressure=0; const char* weather="---"; } env;
    struct { float magnitude=0; const char* status="AMAN"; } seismic;
    struct { unsigned long uptime=0; String version; bool mqttOk=false;
             bool ntpOk=false; float heapKb=0; } sys;

private:
    Adafruit_SSD1306 _oled;
    unsigned long _lastPage = 0;

    // Helper: garis horizontal tipis
    void _hline(int y) {
        _oled.drawFastHLine(0, y, 128, SSD1306_WHITE);
    }

    // Helper: teks kecil di posisi tertentu
    void _txt(int x, int y, const String& s) {
        _oled.setCursor(x, y); _oled.print(s);
    }

    // Helper: header halaman
    void _header(const char* title) {
        _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _oled.setCursor(0, 0); _oled.print(title);
        _hline(9);
    }

    // Helper: bar horizontal sederhana
    void _bar(int x, int y, int w, int h, float pct) {
        _oled.drawRect(x, y, w, h, SSD1306_WHITE);
        int fill = (int)((w - 2) * constrain(pct, 0.0f, 100.0f) / 100.0f);
        if (fill > 0) _oled.fillRect(x + 1, y + 1, fill, h - 2, SSD1306_WHITE);
    }

public:
    OLEDDisplay() : _oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1) {}

    bool begin() {
        isOnline = _oled.begin(SSD1306_SWITCHCAPVCC, ADDR_OLED);
        if (isOnline) { _oled.clearDisplay(); _oled.display(); }
        return isOnline;
    }

    void showSplash(const String& devName) {
        if (!isOnline) return;
        _oled.clearDisplay();
        _oled.setTextColor(SSD1306_WHITE);
        _oled.setTextSize(2);
        _oled.setCursor(0, 4);  _oled.println("BattLab");
        _oled.setTextSize(1);
        _oled.setCursor(0, 26); _oled.println(devName.substring(0, 20));
        _oled.setCursor(0, 38); _oled.print("fw v"); _oled.println(FW_VERSION);
        _oled.setCursor(0, 50); _oled.println("Initializing...");
        _oled.display();
        delay(1500);
    }

    void showAPMode() {
        if (!isOnline) return;
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _oled.setCursor(0, 0);  _oled.println("=== SETUP MODE ===");
        _hline(9);
        _oled.setCursor(0, 14); _oled.println("SSID:");
        _oled.setCursor(0, 24); _oled.println(AP_SSID);
        _oled.setCursor(0, 38); _oled.println("Buka browser:");
        _oled.setCursor(0, 48); _oled.println(AP_IP_STR);
        _oled.display();
    }

    void showConnecting(const String& ssid, int attempt) {
        if (!isOnline) return;
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _oled.setCursor(0, 0);  _oled.println("Connecting WiFi...");
        _hline(9);
        _oled.setCursor(0, 14); _oled.println(ssid.substring(0, 20));
        _oled.setCursor(0, 28); _oled.print("Attempt ");
        _oled.println(attempt / 12 + 1);
        _oled.setCursor(0, 42);
        for (int i = 0; i < (attempt % 16) + 1; i++) _oled.print(".");
        _oled.display();
    }

    void showStatus(const String& l1, const String& l2, const String& l3) {
        if (!isOnline) return;
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _oled.setCursor(0, 4);  _oled.println(l1);
        _oled.setCursor(0, 22); _oled.println(l2);
        _oled.setCursor(0, 40); _oled.println(l3);
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
            case 7: _pageIna();     break;
            default: _pageWifi();   break;
        }
    }

private:
    // ── Hal.0: WiFi + Status ────────────────────────────────────
    void _pageWifi() {
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _header("WiFi Status");
        _oled.setCursor(0, 13);
        if (wifi.wifiOk) {
            _oled.printf("%-4s %s", wifi.mode.c_str(), wifi.ssid.substring(0,14).c_str());
            _oled.setCursor(0, 24); _oled.printf("IP: %s", wifi.ip.c_str());
            _oled.setCursor(0, 35); _oled.printf("RSSI: %d dBm", wifi.rssi);
        } else {
            _oled.printf("%-4s Tidak terhubung", wifi.mode.c_str());
        }
        _oled.setCursor(0, 46);
        _oled.printf("MQTT:%s  SoC:%d%%",
            sys.mqttOk ? "OK" : "--", (int)batt.soc);
        _oled.setCursor(0, 56);
        _oled.printf("NTP:%s  %s",
            sys.ntpOk ? "OK" : "--", batt.status);
        _oled.display();
    }

    // ── Hal.1: Baterai utama ────────────────────────────────────
    void _pageBatt() {
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _header("Battery");
        // Baris 1: Voltase + status
        _oled.setCursor(0, 13);
        _oled.printf("V:%6.3fV  [%s]", batt.voltage, batt.status);
        // Baris 2: Arus
        _oled.setCursor(0, 24);
        _oled.printf("I:%+6.3fA  P:%.2fW", batt.current,
            batt.voltage * fabsf(batt.current));
        // Baris 3: SoC + SoH
        _oled.setCursor(0, 35);
        _oled.printf("SoC:%5.1f%%  SoH:%.0f%%", batt.soc, batt.soh);
        // Bar SoC (y=44, h=7)
        _bar(0, 44, 128, 7, batt.soc);
        // Baris 5: Ri + Siklus
        _oled.setCursor(0, 54);
        _oled.printf("Ri:%.0fmO  Cyc:%d", batt.internalR, energy.cycles);
        _oled.display();
    }

    // ── Hal.2: Power ────────────────────────────────────────────
    void _pagePower() {
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _header("Power");
        float pIn  = batt.current > 0 ? batt.voltage * batt.current    : 0;
        float pOut = batt.current < 0 ? batt.voltage * (-batt.current) : 0;
        _oled.setCursor(0, 13); _oled.printf("IN :  %7.3f W", pIn);
        _oled.setCursor(0, 24); _oled.printf("OUT:  %7.3f W", pOut);
        _oled.setCursor(0, 35); _oled.printf("NET:  %+7.3f W",
            batt.voltage * batt.current);
        _oled.setCursor(0, 46); _oled.printf("SoC:%.1f%%  C:%.2fC",
            batt.soc, batt.cRate);
        _oled.display();
    }

    // ── Hal.3: Akumulasi ────────────────────────────────────────
    void _pageEnergy() {
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _header("Accumulation");
        _oled.setCursor(0, 13); _oled.printf("CHG: %7.0f mAh", energy.mahChg);
        _oled.setCursor(0, 24); _oled.printf("DIS: %7.0f mAh", energy.mahDis);
        _oled.setCursor(0, 35); _oled.printf("Wh+: %7.2f Wh",  energy.whChg);
        _oled.setCursor(0, 46); _oled.printf("Wh-: %7.2f Wh",  energy.whDis);
        _oled.setCursor(0, 56); _oled.printf("Net: %.2f Wh  Cyc:%d",
            energy.whChg - energy.whDis, energy.cycles);
        _oled.display();
    }

    // ── Hal.4: Lingkungan ───────────────────────────────────────
    void _pageEnv() {
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _header("Environment");
        _oled.setCursor(0, 13); _oled.printf("Suhu:  %6.1f oC", env.temp);
        _oled.setCursor(0, 24); _oled.printf("Hum:   %6.1f %%", env.humidity);
        _oled.setCursor(0, 35); _oled.printf("Press: %6.1f hPa", env.pressure);
        _oled.setCursor(0, 46); _oled.printf("Cuaca: %s", env.weather);
        _oled.display();
    }

    // ── Hal.5: Seismik ──────────────────────────────────────────
    void _pageSeismic() {
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _header("Seismic");
        _oled.setCursor(0, 13);
        _oled.setTextSize(2);
        _oled.printf("%.4fg", seismic.magnitude);
        _oled.setTextSize(1);
        _oled.setCursor(0, 35); _oled.printf("Status: %s", seismic.status);
        // Bar magnitude (0..2g)
        _bar(0, 46, 128, 8, seismic.magnitude / 2.0f * 100.0f);
        _oled.setCursor(0, 56); _oled.println("INT1 Hardware ISR");
        _oled.display();
    }

    // ── Hal.6: Sistem ───────────────────────────────────────────
    void _pageSys() {
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _header("System");
        unsigned long s = sys.uptime;
        _oled.setCursor(0, 13); _oled.printf("v%s  Heap:%.0fKB",
            sys.version.c_str(), sys.heapKb);
        _oled.setCursor(0, 24); _oled.printf("IP: %s", wifi.ip.c_str());
        _oled.setCursor(0, 35); _oled.printf("Up: %lud %02lu:%02lu:%02lu",
            s/86400, (s%86400)/3600, (s%3600)/60, s%60);
        _oled.setCursor(0, 46); _oled.printf("MQTT:%-3s NTP:%-3s",
            sys.mqttOk ? "OK" : "NO", sys.ntpOk ? "OK" : "NO");
        _oled.setCursor(0, 56); _oled.printf("Mode: %s", wifi.mode.c_str());
        _oled.display();
    }

    // ── Hal.7: INA226 Debug ─────────────────────────────────────
    void _pageIna() {
        _oled.clearDisplay(); _oled.setTextSize(1); _oled.setTextColor(SSD1306_WHITE);
        _header("INA226 Debug");
        _oled.setCursor(0, 13); _oled.printf("V_raw: %.5fV", batt.voltage);
        _oled.setCursor(0, 24); _oled.printf("I_raw: %+.5fA", batt.current);
        _oled.setCursor(0, 35); _oled.printf("P:     %.4fW", batt.voltage * batt.current);
        _oled.setCursor(0, 46); _oled.printf("Ri:    %.2f mOhm", batt.internalR);
        _oled.setCursor(0, 56); _oled.printf("SoC:%.1f SoH:%.0f%%",
            batt.soc, batt.soh);
        _oled.display();
    }
};

OLEDDisplay oled;