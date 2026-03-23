#pragma once

// ============================================================
// BattAnalyzer — WebServer.h
// ESPAsyncWebServer + WebSocket live data push
// Semua halaman di-serve dari LittleFS (/data/)
// ============================================================

#include <WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncWebSocket.h>
#include <ArduinoJson.h>
#include <Update.h>
#include "Config.h"
#include "NVSManager.h"
#include "SessionManager.h"
#include "Logger.h"
#include "NTPManager.h"
#include "BatteryMonitor.h"
#include "EnvSensor.h"
#include "SeismicSensor.h"
#include "RelayManager.h"
#include "BuzzerManager.h"
#include "OLEDDisplay.h"
#include "MQTTManager.h"

static AsyncWebServer   _server(80);
static AsyncWebSocket   _ws("/ws");

// ── WebSocket push helper — dipanggil dari callback Logger ───
static void _wsPushInternal(const String& json) {
    _ws.textAll(json);
}

// Helper: auth check macro
#define AUTH_OR_REDIRECT(req) \
    if (!sessionMgr.isValid(req)) { req->redirect("/login.html"); return; }

class WebServer {
public:
    // Flag reload MQTT dari setting
    bool mqttReloadReq = false;
    bool ntpReloadReq  = false;

    void begin() {
        if (!LittleFS.begin(true)) {
            Serial.println("[FS] LittleFS mount GAGAL!");
            return;
        }
        Serial.println("[FS] LittleFS OK");

        // Daftarkan callback Logger → WebSocket push
        setWsPushCallback(_wsPushInternal);
        _ws.onEvent([this](AsyncWebSocket* s, AsyncWebSocketClient* c,
                           AwsEventType t, void* a, uint8_t* d, size_t l) {
            _onWsEvent(s, c, t, a, d, l);
        });
        _server.addHandler(&_ws);

        _registerRoutes();
        _server.begin();
        Serial.println("[Web] Server aktif port 80");
    }

    // Panggil dari loop() — push data sensor ke WS tiap 1 detik
    void pushSensorData() {
        if (_ws.count() == 0) return;
        String json = _buildDataJson();
        // Inject type field: ganti '{' pertama dengan '{"type":"data",'
        String out = "{\"type\":\"data\"," + json.substring(1);
        _ws.textAll(out);
    }

private:
    void _onWsEvent(AsyncWebSocket* s, AsyncWebSocketClient* c,
                    AwsEventType t, void* a, uint8_t* d, size_t l) {
        if (t == WS_EVT_CONNECT) {
            Serial.printf("[WS] Client #%u connect\n", c->id());
            // Kirim data awal
            String json = _buildDataJson();
            c->text("{\"type\":\"data\"," + json.substring(1));
        } else if (t == WS_EVT_DISCONNECT) {
            Serial.printf("[WS] Client #%u disconnect\n", c->id());
        }
    }

    // ── Build data JSON untuk WS push dan /api/data ───────────
    String _buildDataJson() {
        JsonDocument doc;

        // Battery
        JsonObject b = doc["batt"].to<JsonObject>();
        b["v"]    = round(battMon.voltage * 1000) / 1000.0;
        b["i"]    = round(battMon.current * 1000) / 1000.0;
        b["p"]    = round(battMon.power   * 100)  / 100.0;
        b["soc"]  = battMon.soc;
        b["soh"]  = round(battMon.soh * 10) / 10.0;
        b["crate"]= round(battMon.cRate * 100) / 100.0;
        b["capreal"] = round(battMon.capReal);
        b["st"]   = battMon.status;
        b["ri"]   = round(battMon.internalR * 10) / 10.0;
        b["rt"]   = round(battMon.runtimeMin * 10) / 10.0;
        b["mahc"] = round(battMon.mahCharge * 10)    / 10.0;
        b["mahd"] = round(battMon.mahDischarge * 10) / 10.0;
        b["whc"]  = round(battMon.whCharge * 100)    / 100.0;
        b["whd"]  = round(battMon.whDischarge * 100) / 100.0;
        b["cyc"]  = battMon.cycleCount;

        // Env
        JsonObject e = doc["env"].to<JsonObject>();
        e["temp"] = round(envSensor.temperature * 10) / 10.0;
        e["hum"]  = round(envSensor.humidity    * 10) / 10.0;
        e["pres"] = round(envSensor.pressure    * 10) / 10.0;
        e["alt"]  = round(envSensor.altitude    * 10) / 10.0;
        e["wx"]   = envSensor.weatherText;
        e["wxi"]  = envSensor.weatherIcon;
        e["aht"]  = envSensor.ahtOnline;
        e["bmp"]  = envSensor.bmpOnline;

        // Seismic
        JsonObject s = doc["seis"].to<JsonObject>();
        s["mag"] = round(seismic.magnitude * 10000) / 10000.0;
        s["st"]  = seismic.statusText();
        s["ax"]  = round(seismic.accX * 1000) / 1000.0;
        s["ay"]  = round(seismic.accY * 1000) / 1000.0;
        s["az"]  = round(seismic.accZ * 1000) / 1000.0;
        s["cal"] = seismic.isCalibrated;
        s["ok"]  = seismic.isOnline;

        // Relay
        JsonArray r = doc["relay"].to<JsonArray>();
        for (int i = 0; i < RELAY_COUNT; i++) r.add(relayMgr.state[i]);

        // System
        doc["uptime"]    = millis() / 1000;
        doc["ip"]        = WiFi.localIP().toString();
        doc["mqtt_ok"]   = mqttMgr.isConnected;
        doc["ntp_time"]  = ntpMgr.getTimeStr();
        doc["ntp_sync"]  = ntpMgr.synced;
        doc["fw_ver"]    = FW_VERSION;

        String out;
        serializeJson(doc, out);
        return out;
    }

    // ── Register semua routes ──────────────────────────────────
    void _registerRoutes() {

        // ── Static files dari LittleFS ─────────────────────────
        // Login — tidak perlu auth
        _server.on("/login.html", HTTP_GET, [](AsyncWebServerRequest* req) {
            req->send(LittleFS, "/login.html", "text/html");
        });

        // Root redirect
        _server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
            if (!sessionMgr.isValid(req)) { req->redirect("/login.html"); return; }
            req->redirect("/dashboard.html");
        });

        // Halaman yang butuh auth
        auto serveAuth = [](const char* path, const char* mime) {
            _server.on(path, HTTP_GET, [path, mime](AsyncWebServerRequest* req) {
                AUTH_OR_REDIRECT(req)
                req->send(LittleFS, path, mime);
            });
        };
        serveAuth("/dashboard.html", "text/html");
        serveAuth("/setting.html",   "text/html");
        serveAuth("/system.html",    "text/html");
        serveAuth("/log.html",       "text/html");

        // ── API: Login ─────────────────────────────────────────
        _server.on("/api/login", HTTP_POST,
            [](AsyncWebServerRequest* req){},
            nullptr,
            [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
                JsonDocument doc;
                if (deserializeJson(doc, data, len)) { req->send(400, "application/json", "{\"ok\":false}"); return; }
                String usr = doc["usr"].as<String>();
                String pwd = doc["pwd"].as<String>();
                if (usr == nvs.getAuthUser() && pwd == nvs.getAuthPass()) {
                    String token = sessionMgr.createSession();
                    AsyncWebServerResponse* res = req->beginResponse(200,
                        "application/json",
                        "{\"ok\":true,\"token\":\"" + token + "\"}");
                    res->addHeader("Set-Cookie", SessionManager::makeCookie(token));
                    req->send(res);
                } else {
                    req->send(200, "application/json", "{\"ok\":false}");
                }
            });

        // ── API: Logout ─────────────────────────────────────────
        _server.on("/api/logout", HTTP_POST, [](AsyncWebServerRequest* req) {
            String token = sessionMgr.extractToken(req);
            sessionMgr.invalidateToken(token);
            AsyncWebServerResponse* res = req->beginResponse(200, "application/json", "{\"ok\":true}");
            res->addHeader("Set-Cookie", SessionManager::clearCookie());
            req->send(res);
        });

        // ── API: Data realtime ──────────────────────────────────
        _server.on("/api/data", HTTP_GET, [this](AsyncWebServerRequest* req) {
            AUTH_OR_REDIRECT(req)
            req->send(200, "application/json", _buildDataJson());
        });

        // ── API: Sparkline ─────────────────────────────────────
        _server.on("/api/spark", HTTP_GET, [](AsyncWebServerRequest* req) {
            AUTH_OR_REDIRECT(req)
            req->send(200, "application/json", battMon.sparklineJson());
        });

        // ── API: History 7 hari ────────────────────────────────
        _server.on("/api/history", HTTP_GET, [](AsyncWebServerRequest* req) {
            AUTH_OR_REDIRECT(req)
            bool useWh = req->hasParam("unit") &&
                         req->getParam("unit")->value() == "wh";
            req->send(200, "application/json", battMon.historyJson(useWh));
        });

        // ── API: Kalibrasi INA ─────────────────────────────────
        _server.on("/api/batt/cal_offset", HTTP_POST, [](AsyncWebServerRequest* req) {
            AUTH_OR_REDIRECT(req)
            battMon.calibrateCurrentOffset();
            char buf[80];
            snprintf(buf, sizeof(buf),
                "{\"ok\":true,\"offset\":%.5f}", nvs.getInaIOffset());
            req->send(200, "application/json", String(buf));
        });

        _server.on("/api/batt/cal_shunt", HTTP_POST,
            [](AsyncWebServerRequest* req){},
            nullptr,
            [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
                AUTH_OR_REDIRECT(req)
                JsonDocument doc;
                if (deserializeJson(doc, data, len)) { req->send(400); return; }
                float actual = doc["actual_a"].as<float>();
                if (fabsf(actual) < 0.1f) {
                    req->send(200, "application/json",
                        "{\"ok\":false,\"msg\":\"Arus terlalu kecil\"}");
                    return;
                }
                battMon.calibrateShuntActual(actual);
                char buf[80];
                snprintf(buf, sizeof(buf),
                    "{\"ok\":true,\"shunt\":%.5f}", nvs.getShuntOhms());
                req->send(200, "application/json", String(buf));
            });

        // ── API: Log ───────────────────────────────────────────
        _server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest* req) {
            AUTH_OR_REDIRECT(req)
            String cat = req->hasParam("cat") ? req->getParam("cat")->value() : "all";
            req->send(200, "application/json", logger.toJson(cat.c_str()));
        });

        _server.on("/api/log/clear", HTTP_POST, [](AsyncWebServerRequest* req) {
            AUTH_OR_REDIRECT(req)
            logger.clear();
            req->send(200, "application/json", "{\"ok\":true}");
        });

        // ── API: Log mask — GET status, POST update ────────────
        _server.on("/api/log/mask", HTTP_GET, [](AsyncWebServerRequest* req) {
            AUTH_OR_REDIRECT(req)
            req->send(200, "application/json", logger.maskToJson());
        });

        _server.on("/api/log/mask", HTTP_POST,
            [](AsyncWebServerRequest* req){},
            nullptr,
            [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
                AUTH_OR_REDIRECT(req)
                JsonDocument doc;
                if (deserializeJson(doc, data, len)) { req->send(400); return; }
                uint32_t m = 0;
                if (doc["SYSTEM"].as<bool>()) m |= LOG_MASK_SYSTEM;
                if (doc["WIFI"].as<bool>())   m |= LOG_MASK_WIFI;
                if (doc["MQTT"].as<bool>())   m |= LOG_MASK_MQTT;
                if (doc["SENSOR"].as<bool>()) m |= LOG_MASK_SENSOR;
                if (doc["BATT"].as<bool>())   m |= LOG_MASK_BATT;
                if (doc["ALERT"].as<bool>())  m |= LOG_MASK_ALERT;
                if (doc["TELE"].as<bool>())   m |= LOG_MASK_TELE;
                if (doc["OTA"].as<bool>())    m |= LOG_MASK_OTA;
                if (doc["DEBUG"].as<bool>())  m |= LOG_MASK_DEBUG;
                logger.setMask(m);
                nvs.setLogMask(m);
                req->send(200, "application/json", "{\"ok\":true}");
            });

        // ── API: WiFi Scan ─────────────────────────────────────
        // ── API: Scan WiFi (async, non-blocking) ──────────────
        _server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
            // Cek hasil scan sebelumnya dulu
            int n = WiFi.scanComplete();
            if (n == WIFI_SCAN_RUNNING) {
                // Scan masih berjalan, minta client retry
                req->send(200, "application/json", "{\"scanning\":true,\"networks\":[]}");
                return;
            }
            if (n == WIFI_SCAN_FAILED || n == 0) {
                // Mulai scan baru (async, tidak block)
                WiFi.scanNetworks(true);
                req->send(200, "application/json", "{\"scanning\":true,\"networks\":[]}");
                return;
            }
            // Hasil scan sudah ada
            String j = "{\"scanning\":false,\"networks\":[";
            for (int i = 0; i < n; i++) {
                if (i > 0) j += ",";
                String ssid = WiFi.SSID(i);
                ssid.replace("\\", "\\\\");
                ssid.replace("\"", "\\\"");
                j += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + WiFi.RSSI(i)
                   + ",\"enc\":" + (WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
            }
            j += "]}";
            WiFi.scanDelete(); // Hapus hasil agar scan berikutnya fresh
            req->send(200, "application/json", j);
        });

        // ── API: Relay toggle ──────────────────────────────────
        _server.on("/api/relay", HTTP_POST,
            [](AsyncWebServerRequest* req){},
            nullptr,
            [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
                AUTH_OR_REDIRECT(req)
                JsonDocument doc;
                if (deserializeJson(doc, data, len)) { req->send(400); return; }
                int  idx = doc["idx"].as<int>() - 1;
                bool on  = doc["on"].as<bool>();
                relayMgr.set(idx, on);
                req->send(200, "application/json", "{\"ok\":true}");
            });

        // ── API: System info ───────────────────────────────────
        _server.on("/api/system", HTTP_GET, [](AsyncWebServerRequest* req) {
            AUTH_OR_REDIRECT(req)
            JsonDocument doc;
            doc["ram_free"]    = ESP.getFreeHeap();
            doc["ram_total"]   = ESP.getHeapSize();
            doc["chip_temp"]   = temperatureRead();
            doc["rssi"]        = WiFi.RSSI();
            doc["cpu_mhz"]     = ESP.getCpuFreqMHz();
            doc["flash_free"]  = ESP.getFreeSketchSpace();
            doc["flash_total"] = ESP.getFlashChipSize();
            doc["uptime"]      = millis() / 1000;
            doc["ip"]          = WiFi.localIP().toString();
            doc["gw"]          = WiFi.gatewayIP().toString();
            doc["mac"]         = WiFi.macAddress();
            doc["host"]        = nvs.getDeviceName();
            doc["ssid"]        = WiFi.SSID();
            doc["fw_ver"]      = FW_VERSION;
            doc["fw_build"]    = FW_BUILD;
            doc["mqtt_ok"]     = mqttMgr.isConnected;
            doc["ntp_sync"]    = ntpMgr.synced;
            doc["ntp_time"]    = ntpMgr.getTimeStr();
            doc["fs_total"]    = LittleFS.totalBytes();
            doc["fs_used"]     = LittleFS.usedBytes();
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        // ── API: Status singkat ────────────────────────────────
        _server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
            AUTH_OR_REDIRECT(req)
            String j = "{\"ok\":true,\"mqtt\":" + String(mqttMgr.isConnected ? "true" : "false") +
                       ",\"ntp\":" + String(ntpMgr.synced ? "true" : "false") + "}";
            req->send(200, "application/json", j);
        });

        // ── API: Get Setting ───────────────────────────────────
        _server.on("/api/setting", HTTP_GET, [](AsyncWebServerRequest* req) {
            AUTH_OR_REDIRECT(req)
            JsonDocument doc;
            // WiFi
            doc["wifi_mode"] = nvs.getWifiMode();
            doc["ssid"]      = nvs.getWifiSSID();
            doc["ip_mode"]   = nvs.getIPMode();
            doc["ip"]        = nvs.getStaticIP();
            doc["gw"]        = nvs.getGateway();
            doc["sn"]        = nvs.getSubnet();
            doc["dns"]       = nvs.getDNS();
            // Device
            doc["dev_name"]  = nvs.getDeviceName();
            doc["ntp_en"]    = nvs.getNtpEn();
            doc["ntp_server"]= nvs.getNtpServer();
            doc["ntp_offset"]= nvs.getNtpOffset();
            // Battery
            doc["v_max"]     = nvs.getVMax();
            doc["v_nom"]     = nvs.getVNom();
            doc["v_cutoff"]  = nvs.getVCutoff();
            doc["i_max"]     = nvs.getIMax();
            doc["cap_nom"]   = nvs.getCapNominal();
            doc["cutoff_en"] = nvs.getCutoffEn();
            doc["shunt"]     = nvs.getShuntOhms() * 1000.0f;
            doc["batt_type"] = nvs.getBattType();
            doc["batt_cells"]= nvs.getBattCells();
            doc["soc_method"]= nvs.getSocMethod();
            doc["cap_real"]  = nvs.getCapReal();
            doc["soh"]       = nvs.getSoH();
            // INA226 advanced
            doc["ina_avg"]   = nvs.getInaAvg();
            doc["ina_vconv"] = nvs.getInaVConv();
            doc["ina_iconv"] = nvs.getInaIConv();
            doc["ina_mode"]  = nvs.getInaMode();
            doc["ina_ioff"]  = nvs.getInaIOffset();
            doc["ina_voff"]  = nvs.getInaVOffset();
            // Alarm config
            JsonArray alarms = doc["alarms"].to<JsonArray>();
            for (int i = 0; i < ALARM_COUNT; i++) {
                JsonObject ao = alarms.add<JsonObject>();
                ao["en"]   = nvs.getAlarmEn(i);
                ao["buzz"] = nvs.getAlarmBuzz(i);
                ao["tele"] = nvs.getAlarmTele(i);
                ao["mqtt"] = nvs.getAlarmMqtt(i);
                ao["cool"] = nvs.getAlarmCool(i);
            }
            // MQTT
            doc["mqtt_en"]    = nvs.getMqttEn();
            doc["mqtt_host"]  = nvs.getMqttHost();
            doc["mqtt_port"]  = nvs.getMqttPort();
            doc["mqtt_user"]  = nvs.getMqttUser();
            doc["mqtt_topic"] = nvs.getMqttTopic();
            doc["mqtt_intv"]  = nvs.getMqttInterval();
            doc["mqtt_qos"]   = nvs.getMqttQos();
            doc["mqtt_ha"]    = nvs.getMqttHa();
            doc["mqtt_mode"]  = nvs.getMqttMode();
            doc["mqtt_wspath"]= nvs.getMqttWsPath();
            // Alert/Seismic
            doc["seis_thr"]   = nvs.getSeismicThr();
            doc["seis_cal"]   = nvs.isSeismicCal();
            // Env offsets
            doc["aht_t_off"]  = nvs.getAhtTempOffset();
            doc["aht_h_off"]  = nvs.getAhtHumOffset();
            doc["bmp_t_off"]  = nvs.getBmpTempOffset();
            // Telegram
            doc["tele_en"]    = nvs.getTeleEn();
            doc["tele_token"] = nvs.getTeleToken();
            doc["tele_cid"]   = nvs.getTeleChatId();
            // OLED
            doc["oled_on"]     = nvs.getOledOn();
            doc["oled_page"]   = nvs.getOledPage();
            doc["oled_scroll"] = nvs.getOledScroll();
            doc["oled_dur"]    = nvs.getOledDur() / 1000;
            doc["oled_bright"] = nvs.getOledBright();
            // Relay schedule
            JsonArray relays = doc["relays"].to<JsonArray>();
            for (int i = 1; i <= RELAY_COUNT; i++) {
                JsonObject ro = relays.add<JsonObject>();
                ro["en"]  = nvs.getRelayEn(i);
                ro["onh"] = nvs.getRelayOnH(i);
                ro["onm"] = nvs.getRelayOnM(i);
                ro["ofh"] = nvs.getRelayOffH(i);
                ro["ofm"] = nvs.getRelayOffM(i);
                ro["day"] = nvs.getRelayDayMask(i);
            }
            // Loc
            doc["loc_name"] = nvs.getLocName();
            doc["loc_lat"]  = nvs.getLocLat();
            doc["loc_lng"]  = nvs.getLocLng();
            String out; serializeJson(doc, out);
            req->send(200, "application/json", out);
        });

        // ── API: Save Setting ──────────────────────────────────
        _server.on("/api/setting", HTTP_POST,
            [](AsyncWebServerRequest* req){},
            nullptr,
            [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
                AUTH_OR_REDIRECT(req)
                JsonDocument body;
                if (deserializeJson(body, data, len)) { req->send(400, "application/json", "{\"ok\":false}"); return; }
                String type = body["type"].as<String>();

                    if (type == "wifi") {
                        nvs.setDeviceName(body["dev_name"].as<String>());
                        nvs.setWifiMode(body["wifi_mode"].as<String>());
                        nvs.setWifiCreds(body["ssid"].as<String>(), body["pass"].as<String>());
                        nvs.setIPMode(body["ip_mode"].as<String>());
                        if (body["ip_mode"].as<String>() == "static")
                            nvs.setStaticIP(body["ip"].as<String>(), body["gw"].as<String>(),
                                            body["sn"].as<String>(), body["dns"].as<String>());
                        req->send(200, "application/json", "{\"ok\":true}");
                        delay(3000); ESP.restart();

                    } else if (type == "device") {
                        nvs.setDeviceName(body["dev_name"].as<String>());
                        nvs.setNtpEn(body["ntp_en"].as<bool>());
                        nvs.setNtpServer(body["ntp_server"].as<String>());
                        nvs.setNtpOffset(body["ntp_offset"].as<int>());
                        req->send(200, "application/json", "{\"ok\":true}");
                        ntpReloadReq = true;

                    } else if (type == "battery") {
                        nvs.setBattType(body["batt_type"].as<String>());
                        nvs.setBattCells(body["batt_cells"].as<int>());
                        nvs.setSocMethod(body["soc_method"].as<int>());
                        nvs.setVMax(body["v_max"].as<float>());
                        nvs.setVNom(body["v_nom"].as<float>());
                        nvs.setVCutoff(body["v_cutoff"].as<float>());
                        nvs.setIMax(body["i_max"].as<float>());
                        nvs.setCapNominal(body["cap_nom"].as<float>());
                        nvs.setCutoffEn(body["cutoff_en"].as<bool>());
                        float shuntMohm = body["shunt"].as<float>();
                        if (shuntMohm > 0.0f) {
                            float ohms = shuntMohm / 1000.0f;
                            nvs.setShuntOhms(ohms);
                            battMon.setShunt(ohms);
                        }
                        req->send(200, "application/json", "{\"ok\":true}");

                    } else if (type == "mqtt") {
                        nvs.setMqtt(body["mqtt_en"].as<bool>(),
                                    body["mqtt_host"].as<String>(),
                                    body["mqtt_port"].as<int>(),
                                    body["mqtt_user"].as<String>(),
                                    body["mqtt_pass"].as<String>(),
                                    body["mqtt_topic"].as<String>(),
                                    body["mqtt_intv"].as<int>(),
                                    body["mqtt_qos"].as<int>(),
                                    body["mqtt_ha"].as<bool>(),
                                    body["mqtt_mode"].as<String>(),
                                    body["mqtt_wspath"].as<String>());
                        req->send(200, "application/json", "{\"ok\":true}");
                        mqttReloadReq = true;

                    } else if (type == "ina") {
                        nvs.setInaAvg(body["ina_avg"].as<int>());
                        nvs.setInaVConv(body["ina_vconv"].as<int>());
                        nvs.setInaIConv(body["ina_iconv"].as<int>());
                        nvs.setInaMode(body["ina_mode"].as<int>());
                        nvs.setInaIOffset(body["ina_ioff"].as<float>());
                        nvs.setInaVOffset(body["ina_voff"].as<float>());
                        battMon.reloadInaConfig();
                        req->send(200, "application/json", "{\"ok\":true}");

                    } else if (type == "alarm") {
                        JsonArray arr = body["alarms"].as<JsonArray>();
                        for (int i = 0; i < (int)arr.size() && i < ALARM_COUNT; i++) {
                            JsonObject ao = arr[i].as<JsonObject>();
                            nvs.setAlarm(i,
                                ao["en"].as<bool>(),
                                ao["buzz"].as<int>(),
                                ao["tele"].as<bool>(),
                                ao["mqtt"].as<bool>(),
                                ao["cool"].as<int>());
                        }
                        req->send(200, "application/json", "{\"ok\":true}");
                        nvs.setSeismicThr(body["seis_thr"].as<float>());
                        seismic.setThreshold(body["seis_thr"].as<float>());
                        if (nvs.isLocSet()) {}  // lokasi sudah set
                        req->send(200, "application/json", "{\"ok\":true}");

                    } else if (type == "seismic_cal") {
                        // Kalibrasi diminta — flag ke main loop
                        extern volatile bool seismicCalReq;
                        seismicCalReq = true;
                        req->send(200, "application/json", "{\"ok\":true}");

                    } else if (type == "env") {
                        nvs.setAhtTempOffset(body["aht_t_off"].as<float>());
                        nvs.setAhtHumOffset(body["aht_h_off"].as<float>());
                        nvs.setBmpTempOffset(body["bmp_t_off"].as<float>());
                        req->send(200, "application/json", "{\"ok\":true}");

                    } else if (type == "telegram") {
                        nvs.setTele(body["tele_en"].as<bool>(),
                                    body["tele_token"].as<String>(),
                                    body["tele_cid"].as<String>());
                        req->send(200, "application/json", "{\"ok\":true}");
                        teleBot.reload();

                    } else if (type == "oled") {
                        nvs.setOledOn(body["oled_on"].as<bool>());
                        nvs.setOledPage(body["oled_page"].as<int>());
                        nvs.setOledScroll(body["oled_scroll"].as<bool>());
                        nvs.setOledDur(body["oled_dur"].as<int>() * 1000);
                        nvs.setOledBright(body["oled_bright"].as<int>());
                        oled.setEnabled(body["oled_on"].as<bool>());
                        oled.setBrightness(body["oled_bright"].as<int>());
                        oled.autoScroll = body["oled_scroll"].as<bool>();
                        oled.pageDurMs  = body["oled_dur"].as<int>() * 1000;
                        if (!oled.autoScroll) oled.setPage(body["oled_page"].as<int>());
                        req->send(200, "application/json", "{\"ok\":true}");

                    } else if (type == "relay") {
                        // Simpan jadwal relay
                        JsonArray arr = body["relays"].as<JsonArray>();
                        for (int i = 0; i < (int)arr.size() && i < RELAY_COUNT; i++) {
                            JsonObject ro = arr[i].as<JsonObject>();
                            nvs.setRelay(i + 1,
                                ro["en"].as<bool>(),
                                ro["onh"].as<int>(), ro["onm"].as<int>(),
                                ro["ofh"].as<int>(), ro["ofm"].as<int>(),
                                ro["day"].as<int>());
                        }
                        req->send(200, "application/json", "{\"ok\":true}");

                    } else if (type == "location") {
                        nvs.setLocation(body["name"].as<String>(),
                                        body["lat"].as<double>(),
                                        body["lng"].as<double>());
                        req->send(200, "application/json", "{\"ok\":true}");

                    } else if (type == "auth") {
                        String u = body["usr"].as<String>();
                        String p = body["pwd"].as<String>();
                        if (u.length() > 0 && p.length() >= 4) {
                            nvs.setAuthUser(u); nvs.setAuthPass(p);
                            sessionMgr.invalidateAll();
                            req->send(200, "application/json", "{\"ok\":true}");
                        } else {
                            req->send(200, "application/json", "{\"ok\":false,\"msg\":\"Password min 4 karakter\"}");
                        }

                    } else if (type == "reset_energy") {
                        battMon.mahCharge = battMon.mahDischarge = 0.0f;
                        battMon.whCharge  = battMon.whDischarge  = 0.0f;
                        nvs.resetEnergy();
                        req->send(200, "application/json", "{\"ok\":true}");

                    } else if (type == "reset_wifi") {
                        nvs.clearWifiOnly();
                        req->send(200, "application/json", "{\"ok\":true}");
                        delay(2000); ESP.restart();

                    } else if (type == "restart") {
                        req->send(200, "application/json", "{\"ok\":true}");
                        delay(3000); ESP.restart();

                    } else if (type == "ntp_sync") {
                        ntpMgr.sync();
                        String t = ntpMgr.synced ? ntpMgr.getTimeStr() : String("");
                        req->send(200, "application/json",
                            "{\"ok\":" + String(ntpMgr.synced ? "true" : "false") +
                            ",\"time\":\"" + t + "\"}");

                    } else {
                        req->send(400, "application/json", "{\"ok\":false,\"msg\":\"Unknown type\"}");
                    }
            });

        // ── API: OTA Upload ────────────────────────────────────
        _server.on("/api/ota", HTTP_POST,
            [](AsyncWebServerRequest* req) {
                if (!sessionMgr.isValid(req)) { req->send(401, "text/plain", "Unauthorized"); return; }
                bool ok = !Update.hasError();
                if (ok) {
                    req->send(200, "text/plain", "OK");
                    String ts = ntpMgr.getTimeStr();
                    logger.add(LOG_OTA, "OTA sukses, restart...", ts.length() ? ts.c_str() : nullptr);
                    delay(1000); ESP.restart();
                } else {
                    req->send(500, "text/plain", String("Gagal: ") + Update.errorString());
                }
            },
            [](AsyncWebServerRequest* req, String fn, size_t idx, uint8_t* data, size_t len, bool final) {
                if (!sessionMgr.isValid(req)) return;
                if (idx == 0) {
                    Serial.printf("[OTA] Upload: %s\n", fn.c_str());
                    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH))
                        Serial.printf("[OTA] begin error: %s\n", Update.errorString());
                }
                if (Update.write(data, len) != len)
                    Serial.printf("[OTA] write error: %s\n", Update.errorString());
                if (final) {
                    if (Update.end(true)) Serial.printf("[OTA] OK %u bytes\n", idx + len);
                    else Serial.printf("[OTA] end error: %s\n", Update.errorString());
                }
            });

        // ── API: OTA Rollback ──────────────────────────────────
        _server.on("/api/ota/rollback", HTTP_POST, [](AsyncWebServerRequest* req) {
            AUTH_OR_REDIRECT(req)
            if (Update.canRollBack()) {
                req->send(200, "application/json", "{\"ok\":true}");
                delay(1000); Update.rollBack(); ESP.restart();
            } else {
                req->send(200, "application/json", "{\"ok\":false,\"msg\":\"Tidak ada firmware lama\"}");
            }
        });

        // ── API: OTA Filesystem (LittleFS) ─────────────────────
        // Upload littlefs.bin → flash ke partisi spiffs (U_SPIFFS)
        // PlatformIO: pio run -t buildfs → .pio/build/.../littlefs.bin
        _server.on("/api/ota/fs", HTTP_POST,
            [](AsyncWebServerRequest* req) {
                if (!sessionMgr.isValid(req)) { req->send(401, "text/plain", "Unauthorized"); return; }
                bool ok = !Update.hasError();
                if (ok) {
                    req->send(200, "text/plain", "OK");
                    String ts = ntpMgr.getTimeStr();
                    logger.add(LOG_OTA, "OTA filesystem sukses, restart...",
                               ts.length() ? ts.c_str() : nullptr);
                    delay(1000); ESP.restart();
                } else {
                    String err = String("Gagal: ") + Update.errorString();
                    Serial.println("[OTA-FS] " + err);
                    req->send(500, "text/plain", err);
                }
            },
            [](AsyncWebServerRequest* req, String fn, size_t idx, uint8_t* data, size_t len, bool final) {
                if (!sessionMgr.isValid(req)) return;
                if (idx == 0) {
                    Serial.printf("[OTA-FS] Start: %s\n", fn.c_str());
                    // Ambil ukuran dari Content-Length header jika ada
                    size_t fsSize = UPDATE_SIZE_UNKNOWN;
                    if (req->hasHeader("Content-Length")) {
                        fsSize = req->header("Content-Length").toInt();
                        Serial.printf("[OTA-FS] Size dari header: %u bytes\n", fsSize);
                    }
                    // Hentikan LittleFS sebelum flash partisi
                    LittleFS.end();
                    if (!Update.begin(fsSize, U_SPIFFS)) {
                        Serial.printf("[OTA-FS] begin error: %s\n", Update.errorString());
                        return;
                    }
                    Serial.println("[OTA-FS] Update.begin OK");
                }
                if (len > 0) {
                    if (Update.write(data, len) != len)
                        Serial.printf("[OTA-FS] write error: %s\n", Update.errorString());
                }
                if (final) {
                    if (Update.end(true))
                        Serial.printf("[OTA-FS] OK %u bytes total\n", idx + len);
                    else
                        Serial.printf("[OTA-FS] end error: %s\n", Update.errorString());
                }
            });

        // ── API: Test Hardware (relay + buzzer) ────────────────
        // GET /api/test/relay?pin=0&state=1  → set relay langsung via GPIO
        // GET /api/test/buzz?pattern=1       → test pola buzzer
        _server.on("/api/test/relay", HTTP_GET, [](AsyncWebServerRequest* req) {
            AUTH_OR_REDIRECT(req)
            int pin   = req->hasParam("pin")   ? req->getParam("pin")->value().toInt()   : 0;
            int state = req->hasParam("state") ? req->getParam("state")->value().toInt() : 0;
            if (pin < 0 || pin > 3) { req->send(400, "application/json", "{\"ok\":false,\"msg\":\"pin 0-3\"}"); return; }
            relayMgr.set(pin, state != 0);
            char buf[80];
            snprintf(buf, sizeof(buf),
                "{\"ok\":true,\"relay\":%d,\"state\":%s,\"gpio\":%d,\"level\":\"%s\"}",
                pin+1, state?"true":"false",
                relayMgr.pins[pin], state?"ON(HIGH)":"OFF(LOW)");
            req->send(200, "application/json", String(buf));
        });

        _server.on("/api/test/buzz", HTTP_GET, [](AsyncWebServerRequest* req) {
            AUTH_OR_REDIRECT(req)
            int pattern = req->hasParam("pattern") ? req->getParam("pattern")->value().toInt() : 1;
            if (pattern < 0 || pattern > 8) pattern = 1;
            buzzer.play((BuzzPattern)pattern);
            char buf[60];
            snprintf(buf, sizeof(buf), "{\"ok\":true,\"pattern\":%d}", pattern);
            req->send(200, "application/json", String(buf));
        });
        _server.on("/setup", HTTP_GET, [](AsyncWebServerRequest* req) {
            req->send(LittleFS, "/setup.html", "text/html");
        });

        _server.on("/api/setup", HTTP_POST,
            [](AsyncWebServerRequest* req){},
            nullptr,
            [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
                JsonDocument json;
                if (deserializeJson(json, data, len)) { req->send(400); return; }
                String savedSSID = json["ssid"].as<String>();
                String savedPass = json["pass"].as<String>();
                Serial.printf("[SETUP] Simpan SSID: '%s' pass_len:%d\n",
                    savedSSID.c_str(), savedPass.length());
                nvs.setWifiMode(json["mode"].as<String>());
                nvs.setDeviceName(json["devname"].as<String>());
                nvs.setWifiCreds(savedSSID, savedPass);
                nvs.setIPMode(json["ip_mode"].as<String>());
                if (json["ip_mode"].as<String>() == "static")
                    nvs.setStaticIP(json["ip"].as<String>(), json["gw"].as<String>(),
                                    json["sn"].as<String>(), json["dns"].as<String>());
                nvs.setSetupDone(true);
                req->send(200, "application/json", "{\"ok\":true}");
                delay(2000); ESP.restart();
            });

        // ── Not Found ──────────────────────────────────────────
        _server.onNotFound([](AsyncWebServerRequest* req) {
            if (!sessionMgr.isValid(req)) { req->redirect("/login.html"); return; }
            req->send(404, "text/plain", "Not found");
        });
    }
};

WebServer webServer;