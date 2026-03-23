#pragma once

// ============================================================
// BattAnalyzer — MQTTManager.h
// Transport layer:
//   TCP / SSL  → PubSubClient (proven, battle-tested)
//   WS  / WSS  → WebSocketsClient + MQTT packet manual
//
// Mode dipilih otomatis dari NVS (mqtt_mode):
//   "tcp" → PubSubClient plain TCP, port 1883
//   "ssl" → PubSubClient + WiFiClientSecure, port 8883
//   "ws"  → WebSocketsClient plain, port 80/9001/1883
//   "wss" → WebSocketsClient SSL, port 443/8884
//
// Subscribe topics (incoming command):
//   {topic}/{devName}/cmd          → "restart" | "relay1_toggle"
//   {topic}/{devName}/relay/1..4   → "ON" | "OFF" | "1" | "0"
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "NVSManager.h"
#include "Logger.h"
#include "BatteryMonitor.h"
#include "EnvSensor.h"
#include "SeismicSensor.h"
#include "RelayManager.h"

// ── Mode constants ────────────────────────────────────────────
#define MQTT_MODE_TCP  0
#define MQTT_MODE_SSL  1
#define MQTT_MODE_WS   2
#define MQTT_MODE_WSS  3

static int _parseMqttMode(const String& m) {
    if (m == "ssl") return MQTT_MODE_SSL;
    if (m == "ws")  return MQTT_MODE_WS;
    if (m == "wss") return MQTT_MODE_WSS;
    return MQTT_MODE_TCP;
}

// ============================================================
// WsMqttBroker — WebSocket transport + MQTT packet manual
// Digunakan untuk mode WS dan WSS
// ============================================================
class WsMqttBroker {
public:
    bool isConnected = false;

private:
    WebSocketsClient  _ws;
    String  _host, _user, _pass, _baseTopic, _devName, _clientId;
    int     _port, _intervalSec, _qos;
    bool    _enabled   = false;
    bool    _wsConn    = false;    // WebSocket layer connected
    bool    _mqttReady = false;    // MQTT CONNACK received
    bool    _haDiscover = false;
    bool    _useSSL    = false;
    unsigned long _lastPublish = 0;
    unsigned long _lastPing    = 0;

    // ── MQTT packet helpers ───────────────────────────────────
    // Tulis length-prefixed string ke buffer (MQTT encoding)
    void _writeStr(uint8_t* buf, int& pos, const String& s) {
        uint16_t len = s.length();
        buf[pos++] = (len >> 8) & 0xFF;
        buf[pos++] = len & 0xFF;
        memcpy(buf + pos, s.c_str(), len);
        pos += len;
    }

    // Encode MQTT remaining length (variable-length encoding)
    int _writeRemLen(uint8_t* buf, int pos, int remLen) {
        do {
            uint8_t b = remLen & 0x7F;
            remLen >>= 7;
            if (remLen > 0) b |= 0x80;
            buf[pos++] = b;
        } while (remLen > 0);
        return pos;
    }

    // ── Kirim MQTT CONNECT packet ─────────────────────────────
    void _sendConnect() {
        bool hasAuth = (_user.length() > 0);
        int payLen = 2 + _clientId.length();
        uint8_t flags = 0x02;  // CleanSession
        if (hasAuth) {
            flags |= 0xC0;  // Username + Password flag
            payLen += 2 + _user.length() + 2 + _pass.length();
        }
        // Remaining length = fixed header variable (10 bytes) + payload
        int remLen = 10 + payLen;

        uint8_t buf[256]; int pos = 0;
        buf[pos++] = 0x10;           // CONNECT packet type
        pos = _writeRemLen(buf, pos, remLen);
        // Protocol name "MQTT" + version 4
        buf[pos++] = 0x00; buf[pos++] = 0x04;
        buf[pos++] = 'M'; buf[pos++] = 'Q';
        buf[pos++] = 'T'; buf[pos++] = 'T';
        buf[pos++] = 0x04;           // Protocol level 4 = MQTT 3.1.1
        buf[pos++] = flags;
        buf[pos++] = 0x00; buf[pos++] = 0x3C;  // Keepalive 60 detik
        _writeStr(buf, pos, _clientId);
        if (hasAuth) {
            _writeStr(buf, pos, _user);
            _writeStr(buf, pos, _pass);
        }
        _ws.sendBIN(buf, pos);
        Serial.printf("[MQTT-WS] CONNECT sent id=%s\n", _clientId.c_str());
    }

    // ── Kirim MQTT PUBLISH packet ─────────────────────────────
    void _sendPublish(const String& topic, const String& payload, bool retain = false) {
        int tLen = topic.length();
        int pLen = payload.length();
        int remLen = 2 + tLen + pLen;

        // Buffer: 1 (type) + 4 (max remlen) + 2 (topiclen) + tLen + pLen
        int bufSize = 7 + tLen + pLen;
        uint8_t* buf = new uint8_t[bufSize];
        int pos = 0;

        buf[pos++] = retain ? 0x31 : 0x30;  // PUBLISH | retain bit
        pos = _writeRemLen(buf, pos, remLen);
        buf[pos++] = (tLen >> 8) & 0xFF;
        buf[pos++] = tLen & 0xFF;
        memcpy(buf + pos, topic.c_str(), tLen); pos += tLen;
        memcpy(buf + pos, payload.c_str(), pLen); pos += pLen;

        _ws.sendBIN(buf, pos);
        delete[] buf;
    }

    // ── Kirim MQTT SUBSCRIBE packet ───────────────────────────
    void _sendSubscribe(const String& topic, uint8_t qos = 0) {
        int tLen = topic.length();
        int remLen = 2 + 2 + tLen + 1;  // packetId + topicLen + topic + qos

        uint8_t buf[256]; int pos = 0;
        buf[pos++] = 0x82;  // SUBSCRIBE packet type
        pos = _writeRemLen(buf, pos, remLen);
        buf[pos++] = 0x00; buf[pos++] = 0x01;  // packet identifier = 1
        buf[pos++] = (tLen >> 8) & 0xFF;
        buf[pos++] = tLen & 0xFF;
        memcpy(buf + pos, topic.c_str(), tLen); pos += tLen;
        buf[pos++] = qos & 0x03;
        _ws.sendBIN(buf, pos);
        Serial.printf("[MQTT-WS] SUBSCRIBE: %s\n", topic.c_str());
    }

    // ── Parse incoming MQTT packet dari WebSocket ─────────────
    void _handlePacket(uint8_t* data, size_t len) {
        if (len < 2) return;
        uint8_t type = data[0] & 0xF0;

        // Debug: print semua byte untuk diagnosa
        Serial.printf("[MQTT-WS] PKT type=0x%02X len=%d bytes:", data[0], (int)len);
        for (size_t i = 0; i < min(len, (size_t)8); i++)
            Serial.printf(" %02X", data[i]);
        Serial.println();

        // CONNACK (0x20) — respons dari broker setelah CONNECT
        // Format: [0x20][0x02][session_present][return_code]
        if (type == 0x20) {
            if (len < 4) {
                Serial.println("[MQTT-WS] CONNACK packet terlalu pendek!");
                return;
            }
            uint8_t returnCode = data[3];  // index 3 = return code
            Serial.printf("[MQTT-WS] CONNACK session=%d rc=%d\n",
                data[2], returnCode);

            if (returnCode == 0x00) {
                _mqttReady  = true;
                isConnected = true;
                Serial.println("[MQTT-WS] CONNACK OK — MQTT ready!");

                String ts = ntpMgr.getTimeStr();
                logger.add(LOG_MQTT, "WS terhubung ke broker",
                           ts.length() ? ts.c_str() : nullptr);

                // Subscribe command + relay topics
                String base = _baseTopic + "/" + _devName;
                _sendSubscribe(base + "/cmd",       (uint8_t)_qos);
                for (int i = 1; i <= RELAY_COUNT; i++) {
                    _sendSubscribe(base + "/relay/" + String(i), (uint8_t)_qos);
                }

                // HA Discovery sekali per session
                if (!_haDiscover && nvs.getMqttHa()) {
                    _publishDiscovery();
                    _haDiscover = true;
                }

                // Publish online status
                delay(100);
                _sendPublish(base + "/status", "online", true);

            } else {
                _mqttReady  = false;
                isConnected = false;
                // Terjemahkan return code ke pesan yang jelas
                const char* rcMsg = "unknown";
                switch (returnCode) {
                    case 1: rcMsg = "bad protocol version"; break;
                    case 2: rcMsg = "client ID rejected";   break;
                    case 3: rcMsg = "server unavailable";   break;
                    case 4: rcMsg = "bad username/password"; break;
                    case 5: rcMsg = "not authorized";       break;
                }
                Serial.printf("[MQTT-WS] CONNACK DITOLAK rc=%d (%s)\n",
                    returnCode, rcMsg);
            }
            return;
        }

        // PUBLISH (0x30) — pesan masuk dari broker (command)
        if (type == 0x30) {
            _handleIncoming(data, len);
            return;
        }

        // SUBACK (0x90) — konfirmasi subscribe berhasil
        if (type == 0x90) {
            Serial.println("[MQTT-WS] SUBACK received");
            return;
        }

        // PINGRESP (0xD0) — respons keepalive
        if (type == 0xD0) return;
    }

    // ── Parse MQTT PUBLISH yang masuk dan jalankan command ────
    void _handleIncoming(uint8_t* data, size_t len) {
        // Decode remaining length
        int pos = 1;
        int remLen = 0, mult = 1;
        while (pos < (int)len) {
            uint8_t b = data[pos++];
            remLen += (b & 0x7F) * mult;
            mult   *= 128;
            if (!(b & 0x80)) break;
        }
        if (pos + 2 > (int)len) return;

        // Topic
        int tLen = (data[pos] << 8) | data[pos + 1]; pos += 2;
        if (pos + tLen > (int)len) return;
        String topic = "";
        for (int i = 0; i < tLen; i++) topic += (char)data[pos++];

        // Payload (sisa bytes)
        String msg = "";
        while (pos < (int)len) msg += (char)data[pos++];

        Serial.printf("[MQTT-WS] CMD: %s = %s\n", topic.c_str(), msg.c_str());
        _dispatchCommand(topic, msg);
    }

    // ── Jalankan command berdasarkan topic ────────────────────
    void _dispatchCommand(const String& topic, const String& msg) {
        // relay/1..4
        for (int i = 0; i < RELAY_COUNT; i++) {
            if (topic.endsWith("/relay/" + String(i + 1))) {
                relayMgr.set(i, msg == "ON" || msg == "1" || msg == "true");
                return;
            }
        }
        // cmd
        if (topic.endsWith("/cmd")) {
            if (msg == "restart")       ESP.restart();
            if (msg == "relay1_toggle") relayMgr.toggle(0);
        }
    }

    // ── HA Discovery ──────────────────────────────────────────
    void _publishDiscovery() {
        String devId = _devName;
        devId.toLowerCase();
        devId.replace(" ", "_");

        String battTopic = _baseTopic + "/" + _devName + "/sensor/battery";
        String avtyTopic = _baseTopic + "/" + _devName + "/status";

        auto pub = [&](const String& sId, JsonDocument& doc) {
            String t = "homeassistant/sensor/" + devId + "/" + sId + "/config";
            String p; serializeJson(doc, p);
            _sendPublish(t, p, true);
            delay(50);
        };

        auto makeDevice = [&](JsonDocument& doc) {
            JsonObject dev = doc["device"].to<JsonObject>();
            dev["name"]         = _devName;
            dev["identifiers"]  = devId;
            dev["model"]        = "ESP32-C3 BattAnalyzer";
            dev["manufacturer"] = "BattLab";
            dev["sw_version"]   = FW_VERSION;
        };

        // Battery entities
        { JsonDocument d; makeDevice(d);
          d["name"]="Battery Voltage"; d["unique_id"]=devId+"_volt";
          d["state_topic"]=battTopic; d["value_template"]="{{ value_json.v }}";
          d["unit_of_measurement"]="V"; d["device_class"]="voltage";
          d["availability_topic"]=avtyTopic; pub("voltage", d); }

        { JsonDocument d; makeDevice(d);
          d["name"]="Battery Current"; d["unique_id"]=devId+"_curr";
          d["state_topic"]=battTopic; d["value_template"]="{{ value_json.i }}";
          d["unit_of_measurement"]="A"; d["device_class"]="current";
          d["availability_topic"]=avtyTopic; pub("current", d); }

        { JsonDocument d; makeDevice(d);
          d["name"]="Battery Power"; d["unique_id"]=devId+"_pwr";
          d["state_topic"]=battTopic; d["value_template"]="{{ value_json.p }}";
          d["unit_of_measurement"]="W"; d["device_class"]="power";
          d["availability_topic"]=avtyTopic; pub("power", d); }

        { JsonDocument d; makeDevice(d);
          d["name"]="Battery SoC"; d["unique_id"]=devId+"_soc";
          d["state_topic"]=battTopic; d["value_template"]="{{ value_json.soc }}";
          d["unit_of_measurement"]="%"; d["device_class"]="battery";
          d["availability_topic"]=avtyTopic; pub("soc", d); }

        { JsonDocument d; makeDevice(d);
          d["name"]="Internal Resistance"; d["unique_id"]=devId+"_ri";
          d["state_topic"]=battTopic; d["value_template"]="{{ value_json.ri }}";
          d["unit_of_measurement"]="mΩ";
          d["availability_topic"]=avtyTopic; pub("internal_r", d); }

        // Env entities
        if (envSensor.ahtOnline || envSensor.bmpOnline) {
            String envTopic = _baseTopic + "/" + _devName + "/sensor/env";
            { JsonDocument d; makeDevice(d);
              d["name"]="Temperature"; d["unique_id"]=devId+"_temp";
              d["state_topic"]=envTopic; d["value_template"]="{{ value_json.temp }}";
              d["unit_of_measurement"]="°C"; d["device_class"]="temperature";
              d["availability_topic"]=avtyTopic; pub("temperature", d); }
            { JsonDocument d; makeDevice(d);
              d["name"]="Humidity"; d["unique_id"]=devId+"_hum";
              d["state_topic"]=envTopic; d["value_template"]="{{ value_json.hum }}";
              d["unit_of_measurement"]="%"; d["device_class"]="humidity";
              d["availability_topic"]=avtyTopic; pub("humidity", d); }
            { JsonDocument d; makeDevice(d);
              d["name"]="Pressure"; d["unique_id"]=devId+"_pres";
              d["state_topic"]=envTopic; d["value_template"]="{{ value_json.pres }}";
              d["unit_of_measurement"]="hPa"; d["device_class"]="atmospheric_pressure";
              d["availability_topic"]=avtyTopic; pub("pressure", d); }
        }

        // Seismic entity
        if (seismic.isOnline) {
            String seisTopic = _baseTopic + "/" + _devName + "/sensor/seismic";
            { JsonDocument d; makeDevice(d);
              d["name"]="Seismic Magnitude"; d["unique_id"]=devId+"_seis";
              d["state_topic"]=seisTopic; d["value_template"]="{{ value_json.mag }}";
              d["unit_of_measurement"]="g";
              d["availability_topic"]=avtyTopic; pub("seismic", d); }
        }

        Serial.println("[MQTT-WS] HA Discovery published");
    }

public:
    void configure(bool en, const String& host, int port,
                   const String& user, const String& pass,
                   const String& topic, int interval, int qos,
                   bool ssl, const String& wsPath) {
        _enabled     = en;
        _host        = host;
        _port        = port;
        _user        = user;
        _pass        = pass;
        _baseTopic   = topic;
        _intervalSec = interval;
        _qos         = qos;
        _useSSL      = ssl;
        _devName     = nvs.getDeviceName();
        _clientId    = "batt-" + _devName;

        if (!en || host.length() == 0) return;

        // Pastikan path diawali "/"
        String path = wsPath.length() > 0 ? wsPath : "/";
        if (!path.startsWith("/")) path = "/" + path;

        Serial.printf("[MQTT-WS] Connecting: %s://%s:%d%s\n",
            ssl ? "wss" : "ws", host.c_str(), port, path.c_str());

        if (ssl) {
            // Cloudflare Tunnel pakai sertifikat Let's Encrypt yang valid
            // beginSSL tanpa fingerprint = skip verifikasi (setInsecure equivalent)
            _ws.beginSSL(host.c_str(), port, path.c_str(),
                         /* fingerprint */ "",
                         /* protocol   */ "mqtt");
        } else {
            _ws.begin(host.c_str(), port, path.c_str(), "mqtt");
        }

        _ws.setReconnectInterval(5000);

        // CATATAN: Jangan tambah setExtraHeaders untuk Host atau
        // Sec-WebSocket-Protocol — library sudah otomatis mengisinya
        // saat memanggil beginSSL/begin dengan parameter "mqtt".
        // Duplicate headers menyebabkan Cloudflare reject (error 400).

        _ws.onEvent([this](WStype_t t, uint8_t* d, size_t l) {
            if (t == WStype_CONNECTED) {
                _wsConn = true;
                Serial.printf("[MQTT-WS] WS layer connected → kirim MQTT CONNECT\n");
                _sendConnect();
            } else if (t == WStype_DISCONNECTED) {
                _wsConn     = false;
                _mqttReady  = false;
                isConnected = false;
                Serial.println("[MQTT-WS] WS layer disconnected");
                String ts = ntpMgr.getTimeStr();
                logger.add(LOG_MQTT, "WS terputus",
                           ts.length() ? ts.c_str() : nullptr);
            } else if (t == WStype_BIN) {
                // Beberapa broker kirim MQTT frame sebagai binary
                Serial.printf("[MQTT-WS] RX BIN %d bytes, type=0x%02X\n",
                    (int)l, l > 0 ? d[0] : 0);
                _handlePacket(d, l);
            } else if (t == WStype_TEXT) {
                // Beberapa broker kirim MQTT frame sebagai text (salah, tapi terjadi)
                Serial.printf("[MQTT-WS] RX TEXT %d bytes, type=0x%02X\n",
                    (int)l, l > 0 ? d[0] : 0);
                _handlePacket(d, l);
            } else if (t == WStype_ERROR) {
                Serial.printf("[MQTT-WS] WS ERROR code=%d\n", (int)l);
            } else if (t == WStype_PING) {
                Serial.println("[MQTT-WS] WS PING received");
            } else if (t == WStype_PONG) {
                Serial.println("[MQTT-WS] WS PONG received");
            }
        });

        Serial.printf("[MQTT-WS] Configured: %s:%d SSL:%s\n",
            host.c_str(), port, ssl ? "yes" : "no");
    }

    void handle() {
        if (!_enabled || _host.length() == 0) return;
        if (WiFi.status() != WL_CONNECTED) { isConnected = false; return; }

        _ws.loop();

        if (!_mqttReady) return;

        // Keepalive PINGREQ tiap 30 detik
        if (millis() - _lastPing >= 30000) {
            uint8_t ping[2] = {0xC0, 0x00};
            _ws.sendBIN(ping, 2);
            _lastPing = millis();
        }

        // Publish data sesuai interval
        if (millis() - _lastPublish >= (unsigned long)(_intervalSec * 1000UL)) {
            _publishData();
            _lastPublish = millis();
        }
    }

    void publishAlert(const String& type, const String& detail) {
        if (!_mqttReady) return;
        String ts  = ntpMgr.getTimeStr();
        String top = _baseTopic + "/" + _devName + "/alert";
        String j   = "{\"device\":\"" + _devName +
                     "\",\"type\":\"" + type +
                     "\",\"detail\":\"" + detail +
                     "\",\"ts\":\"" + ts + "\"}";
        _sendPublish(top, j);
    }

    void disconnect() {
        // MQTT DISCONNECT packet
        uint8_t disc[2] = {0xE0, 0x00};
        if (_wsConn) _ws.sendBIN(disc, 2);
        _ws.disconnect();
        _wsConn     = false;
        _mqttReady  = false;
        isConnected = false;
        _haDiscover = false;
    }

private:
    void _publishData() {
        String base = _baseTopic + "/" + _devName;

        // Battery
        {
            JsonDocument doc;
            doc["v"]    = round(battMon.voltage  * 1000) / 1000.0;
            doc["i"]    = round(battMon.current  * 1000) / 1000.0;
            doc["p"]    = round(battMon.power    * 100)  / 100.0;
            doc["soc"]  = battMon.soc;
            doc["st"]   = battMon.status;
            doc["mahc"] = round(battMon.mahCharge    * 10)  / 10.0;
            doc["mahd"] = round(battMon.mahDischarge * 10)  / 10.0;
            doc["whc"]  = round(battMon.whCharge     * 100) / 100.0;
            doc["whd"]  = round(battMon.whDischarge  * 100) / 100.0;
            doc["ri"]   = round(battMon.internalR    * 10)  / 10.0;
            doc["cyc"]  = battMon.cycleCount;
            String out; serializeJson(doc, out);
            _sendPublish(base + "/sensor/battery", out, true);
        }

        // Env
        if (envSensor.ahtOnline || envSensor.bmpOnline) {
            JsonDocument doc;
            doc["temp"] = round(envSensor.temperature * 10) / 10.0;
            doc["hum"]  = round(envSensor.humidity    * 10) / 10.0;
            doc["pres"] = round(envSensor.pressure    * 10) / 10.0;
            doc["alt"]  = round(envSensor.altitude    * 10) / 10.0;
            doc["wx"]   = envSensor.weatherText;
            String out; serializeJson(doc, out);
            _sendPublish(base + "/sensor/env", out, true);
        }

        // Seismic
        if (seismic.isOnline) {
            JsonDocument doc;
            doc["mag"] = round(seismic.magnitude * 10000) / 10000.0;
            doc["st"]  = seismic.statusText();
            doc["ax"]  = round(seismic.accX * 1000) / 1000.0;
            doc["ay"]  = round(seismic.accY * 1000) / 1000.0;
            doc["az"]  = round(seismic.accZ * 1000) / 1000.0;
            String out; serializeJson(doc, out);
            _sendPublish(base + "/sensor/seismic", out, true);
        }

        // Relay states
        for (int i = 0; i < RELAY_COUNT; i++) {
            _sendPublish(base + "/relay/" + String(i + 1),
                         relayMgr.state[i] ? "ON" : "OFF", true);
        }
    }
};

// ============================================================
// TcpMqttBroker — PubSubClient untuk TCP dan SSL
// ============================================================
class TcpMqttBroker {
public:
    bool isConnected = false;

private:
    WiFiClient        _wifiClient;
    WiFiClientSecure  _secureClient;
    PubSubClient      _client;
    bool    _useSSL      = false;
    bool    _enabled     = false;
    bool    _haDiscover  = false;
    String  _host, _user, _pass, _baseTopic, _devName;
    int     _port, _intervalSec, _qos;
    unsigned long _lastPublish  = 0;
    unsigned long _lastReconn   = 0;

public:
    TcpMqttBroker() : _client(_wifiClient) {}

    void configure(bool en, const String& host, int port,
                   const String& user, const String& pass,
                   const String& topic, int interval, int qos, bool ssl) {
        _enabled     = en;
        _host        = host;
        _port        = port;
        _user        = user;
        _pass        = pass;
        _baseTopic   = topic;
        _intervalSec = interval;
        _qos         = qos;
        _useSSL      = ssl;
        _devName     = nvs.getDeviceName();

        if (!en || host.length() == 0) return;

        if (_useSSL) {
            _secureClient.setInsecure();  // skip cert verification
            _client.setClient(_secureClient);
        } else {
            _client.setClient(_wifiClient);
        }

        _client.setServer(host.c_str(), port);
        _client.setBufferSize(2048);
        _client.setKeepAlive(60);
        _client.setSocketTimeout(10);

        // Callback untuk incoming message (relay command)
        _client.setCallback([this](char* topic, byte* payload, unsigned int len) {
            String t = String(topic);
            String msg = "";
            for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
            Serial.printf("[MQTT-TCP] CMD: %s = %s\n", topic, msg.c_str());
            _dispatchCommand(t, msg);
        });

        Serial.printf("[MQTT-TCP] Configured: %s:%d SSL:%s\n",
            host.c_str(), port, ssl ? "yes" : "no");
    }

    void handle() {
        if (!_enabled || _host.length() == 0) return;
        if (WiFi.status() != WL_CONNECTED) { isConnected = false; return; }

        if (!_client.connected()) {
            isConnected = false;
            unsigned long now = millis();
            if (now - _lastReconn >= 5000) {
                _lastReconn = now;
                _reconnect();
            }
            return;
        }

        _client.loop();
        isConnected = true;

        if (millis() - _lastPublish >= (unsigned long)(_intervalSec * 1000UL)) {
            _publishData();
            _lastPublish = millis();
        }
    }

    void publishAlert(const String& type, const String& detail) {
        if (!isConnected) return;
        String ts  = ntpMgr.getTimeStr();
        String top = _baseTopic + "/" + _devName + "/alert";
        String j   = "{\"device\":\"" + _devName +
                     "\",\"type\":\"" + type +
                     "\",\"detail\":\"" + detail +
                     "\",\"ts\":\"" + ts + "\"}";
        _client.publish(top.c_str(), j.c_str());
    }

    void disconnect() {
        if (_client.connected()) {
            _client.publish((_baseTopic + "/" + _devName + "/status").c_str(),
                            "offline", true);
            _client.disconnect();
        }
        isConnected = false;
        _haDiscover = false;
    }

private:
    void _reconnect() {
        String clientId = "batt-" + _devName;
        String willTopic = _baseTopic + "/" + _devName + "/status";

        Serial.printf("[MQTT-TCP] Konek ke %s:%d (user:%s SSL:%s)...\n",
            _host.c_str(), _port,
            _user.length() > 0 ? _user.c_str() : "anonymous",
            _useSSL ? "yes" : "no");

        bool ok = (_user.length() > 0)
            ? _client.connect(clientId.c_str(),
                              _user.c_str(), _pass.c_str(),
                              willTopic.c_str(), _qos, true, "offline")
            : _client.connect(clientId.c_str(), nullptr, nullptr,
                              willTopic.c_str(), _qos, true, "offline");

        if (ok) {
            isConnected = true;
            Serial.println("[MQTT-TCP] Terhubung!");
            String ts = ntpMgr.getTimeStr();
            logger.add(LOG_MQTT, "TCP terhubung ke broker",
                       ts.length() ? ts.c_str() : nullptr);

            // Subscribe command + relay topics
            String base = _baseTopic + "/" + _devName;
            _client.subscribe((base + "/cmd").c_str(), _qos);
            for (int i = 1; i <= RELAY_COUNT; i++) {
                _client.subscribe((base + "/relay/" + String(i)).c_str(), _qos);
            }

            // HA Discovery
            if (!_haDiscover && nvs.getMqttHa()) {
                _publishDiscovery();
                _haDiscover = true;
            }

            delay(100);
            _client.publish(willTopic.c_str(), "online", true);

        } else {
            int rc = _client.state();
            const char* rcMsg = "unknown";
            switch (rc) {
                case -4: rcMsg = "TIMEOUT — host tidak merespons"; break;
                case -3: rcMsg = "CONNECTION_LOST — koneksi putus"; break;
                case -2: rcMsg = "CONNECT_FAILED — gagal TCP connect"; break;
                case -1: rcMsg = "DISCONNECTED"; break;
                case  1: rcMsg = "BAD_PROTOCOL"; break;
                case  2: rcMsg = "ID_REJECTED"; break;
                case  3: rcMsg = "SERVER_UNAVAILABLE"; break;
                case  4: rcMsg = "BAD_CREDENTIALS — cek user/pass"; break;
                case  5: rcMsg = "UNAUTHORIZED"; break;
            }
            Serial.printf("[MQTT-TCP] Gagal rc=%d: %s\n", rc, rcMsg);
        }
    }

    void _dispatchCommand(const String& topic, const String& msg) {
        for (int i = 0; i < RELAY_COUNT; i++) {
            if (topic.endsWith("/relay/" + String(i + 1))) {
                relayMgr.set(i, msg == "ON" || msg == "1" || msg == "true");
                return;
            }
        }
        if (topic.endsWith("/cmd")) {
            if (msg == "restart")       ESP.restart();
            if (msg == "relay1_toggle") relayMgr.toggle(0);
        }
    }

    void _publishDiscovery() {
        String devId = _devName;
        devId.toLowerCase();
        devId.replace(" ", "_");

        String battTopic = _baseTopic + "/" + _devName + "/sensor/battery";
        String avtyTopic = _baseTopic + "/" + _devName + "/status";

        auto pub = [&](const String& sId, JsonDocument& doc) {
            String t = "homeassistant/sensor/" + devId + "/" + sId + "/config";
            String p; serializeJson(doc, p);
            _client.publish(t.c_str(), p.c_str(), true);
            delay(50);
        };

        auto makeDevice = [&](JsonDocument& doc) {
            JsonObject dev = doc["device"].to<JsonObject>();
            dev["name"]         = _devName;
            dev["identifiers"]  = devId;
            dev["model"]        = "ESP32-C3 BattAnalyzer";
            dev["manufacturer"] = "BattLab";
            dev["sw_version"]   = FW_VERSION;
        };

        { JsonDocument d; makeDevice(d);
          d["name"]="Battery Voltage"; d["unique_id"]=devId+"_volt";
          d["state_topic"]=battTopic; d["value_template"]="{{ value_json.v }}";
          d["unit_of_measurement"]="V"; d["device_class"]="voltage";
          d["availability_topic"]=avtyTopic; pub("voltage", d); }

        { JsonDocument d; makeDevice(d);
          d["name"]="Battery Current"; d["unique_id"]=devId+"_curr";
          d["state_topic"]=battTopic; d["value_template"]="{{ value_json.i }}";
          d["unit_of_measurement"]="A"; d["device_class"]="current";
          d["availability_topic"]=avtyTopic; pub("current", d); }

        { JsonDocument d; makeDevice(d);
          d["name"]="Battery Power"; d["unique_id"]=devId+"_pwr";
          d["state_topic"]=battTopic; d["value_template"]="{{ value_json.p }}";
          d["unit_of_measurement"]="W"; d["device_class"]="power";
          d["availability_topic"]=avtyTopic; pub("power", d); }

        { JsonDocument d; makeDevice(d);
          d["name"]="Battery SoC"; d["unique_id"]=devId+"_soc";
          d["state_topic"]=battTopic; d["value_template"]="{{ value_json.soc }}";
          d["unit_of_measurement"]="%"; d["device_class"]="battery";
          d["availability_topic"]=avtyTopic; pub("soc", d); }

        { JsonDocument d; makeDevice(d);
          d["name"]="Internal Resistance"; d["unique_id"]=devId+"_ri";
          d["state_topic"]=battTopic; d["value_template"]="{{ value_json.ri }}";
          d["unit_of_measurement"]="mΩ";
          d["availability_topic"]=avtyTopic; pub("internal_r", d); }

        if (envSensor.ahtOnline || envSensor.bmpOnline) {
            String envTopic = _baseTopic + "/" + _devName + "/sensor/env";
            { JsonDocument d; makeDevice(d);
              d["name"]="Temperature"; d["unique_id"]=devId+"_temp";
              d["state_topic"]=envTopic; d["value_template"]="{{ value_json.temp }}";
              d["unit_of_measurement"]="°C"; d["device_class"]="temperature";
              d["availability_topic"]=avtyTopic; pub("temperature", d); }
            { JsonDocument d; makeDevice(d);
              d["name"]="Humidity"; d["unique_id"]=devId+"_hum";
              d["state_topic"]=envTopic; d["value_template"]="{{ value_json.hum }}";
              d["unit_of_measurement"]="%"; d["device_class"]="humidity";
              d["availability_topic"]=avtyTopic; pub("humidity", d); }
            { JsonDocument d; makeDevice(d);
              d["name"]="Pressure"; d["unique_id"]=devId+"_pres";
              d["state_topic"]=envTopic; d["value_template"]="{{ value_json.pres }}";
              d["unit_of_measurement"]="hPa"; d["device_class"]="atmospheric_pressure";
              d["availability_topic"]=avtyTopic; pub("pressure", d); }
        }

        if (seismic.isOnline) {
            String seisTopic = _baseTopic + "/" + _devName + "/sensor/seismic";
            { JsonDocument d; makeDevice(d);
              d["name"]="Seismic Magnitude"; d["unique_id"]=devId+"_seis";
              d["state_topic"]=seisTopic; d["value_template"]="{{ value_json.mag }}";
              d["unit_of_measurement"]="g";
              d["availability_topic"]=avtyTopic; pub("seismic", d); }
        }

        Serial.println("[MQTT-TCP] HA Discovery published");
    }

    void _publishData() {
        String base = _baseTopic + "/" + _devName;

        {
            JsonDocument doc;
            doc["v"]    = round(battMon.voltage  * 1000) / 1000.0;
            doc["i"]    = round(battMon.current  * 1000) / 1000.0;
            doc["p"]    = round(battMon.power    * 100)  / 100.0;
            doc["soc"]  = battMon.soc;
            doc["st"]   = battMon.status;
            doc["mahc"] = round(battMon.mahCharge    * 10)  / 10.0;
            doc["mahd"] = round(battMon.mahDischarge * 10)  / 10.0;
            doc["whc"]  = round(battMon.whCharge     * 100) / 100.0;
            doc["whd"]  = round(battMon.whDischarge  * 100) / 100.0;
            doc["ri"]   = round(battMon.internalR    * 10)  / 10.0;
            doc["cyc"]  = battMon.cycleCount;
            String out; serializeJson(doc, out);
            _client.publish((base + "/sensor/battery").c_str(), out.c_str(), true);
        }

        if (envSensor.ahtOnline || envSensor.bmpOnline) {
            JsonDocument doc;
            doc["temp"] = round(envSensor.temperature * 10) / 10.0;
            doc["hum"]  = round(envSensor.humidity    * 10) / 10.0;
            doc["pres"] = round(envSensor.pressure    * 10) / 10.0;
            doc["alt"]  = round(envSensor.altitude    * 10) / 10.0;
            doc["wx"]   = envSensor.weatherText;
            String out; serializeJson(doc, out);
            _client.publish((base + "/sensor/env").c_str(), out.c_str(), true);
        }

        if (seismic.isOnline) {
            JsonDocument doc;
            doc["mag"] = round(seismic.magnitude * 10000) / 10000.0;
            doc["st"]  = seismic.statusText();
            doc["ax"]  = round(seismic.accX * 1000) / 1000.0;
            doc["ay"]  = round(seismic.accY * 1000) / 1000.0;
            doc["az"]  = round(seismic.accZ * 1000) / 1000.0;
            String out; serializeJson(doc, out);
            _client.publish((base + "/sensor/seismic").c_str(), out.c_str(), true);
        }

        for (int i = 0; i < RELAY_COUNT; i++) {
            _client.publish((base + "/relay/" + String(i + 1)).c_str(),
                            relayMgr.state[i] ? "ON" : "OFF", true);
        }
    }
};

// ============================================================
// MQTTManager — wrapper publik, auto-pilih transport
// ============================================================
class MQTTManager {
public:
    bool isConnected = false;

private:
    WsMqttBroker*  _ws  = nullptr;
    TcpMqttBroker* _tcp = nullptr;
    int            _mode     = MQTT_MODE_TCP;
    bool           _enabled  = false;
    bool           _configured = false;

public:
    MQTTManager() {}

    ~MQTTManager() {
        if (_ws)  { delete _ws;  _ws  = nullptr; }
        if (_tcp) { delete _tcp; _tcp = nullptr; }
    }

    void begin() {
        if (!nvs.getMqttEn() || nvs.getMqttHost().length() == 0) return;

        _mode    = _parseMqttMode(nvs.getMqttMode());
        _enabled = true;

        // Bersihkan instance lama
        if (_ws)  { delete _ws;  _ws  = nullptr; }
        if (_tcp) { delete _tcp; _tcp = nullptr; }
        isConnected = false;

        bool en       = nvs.getMqttEn();
        String host   = nvs.getMqttHost();
        int    port   = nvs.getMqttPort();
        String user   = nvs.getMqttUser();
        String pass   = nvs.getMqttPass();
        String topic  = nvs.getMqttTopic();
        int    intv   = nvs.getMqttInterval();
        int    qos    = nvs.getMqttQos();
        String wsPath = nvs.getMqttWsPath();

        const char* modeStr[] = {"TCP", "SSL", "WS", "WSS"};
        Serial.printf("[MQTT] Mode: %s (%s:%d)\n",
            modeStr[_mode], host.c_str(), port);
        Serial.printf("[MQTT] Topic: %s | Interval: %ds | QoS: %d | HA: %s\n",
            topic.c_str(), intv, qos, nvs.getMqttHa() ? "yes" : "no");

        if (_mode == MQTT_MODE_WS || _mode == MQTT_MODE_WSS) {
            _ws = new WsMqttBroker();
            _ws->configure(en, host, port, user, pass, topic, intv, qos,
                           _mode == MQTT_MODE_WSS, wsPath);
        } else {
            _tcp = new TcpMqttBroker();
            _tcp->configure(en, host, port, user, pass, topic, intv, qos,
                            _mode == MQTT_MODE_SSL);
        }

        _configured = true;
    }

    void handle() {
        if (!_configured || !_enabled) return;
        if (WiFi.status() != WL_CONNECTED) { isConnected = false; return; }

        if (_mode == MQTT_MODE_WS || _mode == MQTT_MODE_WSS) {
            if (_ws) { _ws->handle(); isConnected = _ws->isConnected; }
        } else {
            if (_tcp) { _tcp->handle(); isConnected = _tcp->isConnected; }
        }
    }

    void publishAlert(const String& type, const String& detail) {
        if (!isConnected) return;
        if ((_mode == MQTT_MODE_WS || _mode == MQTT_MODE_WSS) && _ws)
            _ws->publishAlert(type, detail);
        else if (_tcp)
            _tcp->publishAlert(type, detail);
    }

    void reload() {
        Serial.println("[MQTT] Reload...");
        if (_ws)  _ws->disconnect();
        if (_tcp) _tcp->disconnect();
        _configured = false;
        _enabled    = false;
        delay(300);
        begin();
    }
};

MQTTManager mqttMgr;