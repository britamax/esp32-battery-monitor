#pragma once

// ============================================================
// BattAnalyzer — MQTTManager.h
// PubSubClient — TCP:1883 / SSL:8883 / WSS:443
// Home Assistant MQTT Discovery otomatis
// ============================================================

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "NVSManager.h"
#include "Logger.h"
#include "BatteryMonitor.h"
#include "EnvSensor.h"
#include "SeismicSensor.h"
#include "RelayManager.h"

class MQTTManager {
public:
    bool isConnected = false;

private:
    WiFiClient       _wifiClient;
    WiFiClientSecure _secureClient;
    PubSubClient     _client;
    bool     _useSSL     = false;
    bool     _haDiscover = false;
    unsigned long _lastPublish = 0;
    unsigned long _lastReconn  = 0;

    String _topic;   // base topic = nvs.getMqttTopic() / devName

public:
    MQTTManager() : _client(_wifiClient) {}

    void begin() {
        if (!nvs.getMqttEn() || nvs.getMqttHost().length() == 0) return;

        int port = nvs.getMqttPort();
        _useSSL  = (port == 8883);
        _topic   = nvs.getMqttTopic() + "/" + nvs.getDeviceName();

        if (_useSSL) {
            _secureClient.setInsecure();
            _client.setClient(_secureClient);
        } else {
            _client.setClient(_wifiClient);
        }

        _client.setServer(nvs.getMqttHost().c_str(), port);
        _client.setBufferSize(2048);
        _client.setKeepAlive(60);
        _client.setCallback([this](char* topic, byte* payload, unsigned int len) {
            _onMessage(topic, payload, len);
        });
        Serial.printf("[MQTT] Configured: %s:%d SSL:%s\n",
            nvs.getMqttHost().c_str(), port, _useSSL ? "yes" : "no");
    }

    void handle() {
        if (!nvs.getMqttEn() || nvs.getMqttHost().length() == 0) return;
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

        // Publish data sesuai interval
        unsigned long now = millis();
        if (now - _lastPublish >= (unsigned long)(nvs.getMqttInterval() * 1000UL)) {
            _publishData();
            _lastPublish = now;
        }
    }

    void publishAlert(const String& type, const String& detail) {
        if (!isConnected) return;
        String ts = ntpMgr.getTimeStr();
        String j = "{\"device\":\"" + nvs.getDeviceName() +
                   "\",\"type\":\"" + type +
                   "\",\"detail\":\"" + detail +
                   "\",\"ts\":\"" + ts + "\"}";
        _client.publish((_topic + "/alert").c_str(), j.c_str());
    }

    void reload() {
        if (_client.connected()) {
            _client.publish((_topic + "/status").c_str(), "offline", true);
            _client.disconnect();
        }
        isConnected  = false;
        _haDiscover  = false;
        begin();
    }

private:
    void _reconnect() {
        String clientId = "batt-" + nvs.getDeviceName();
        String user = nvs.getMqttUser();
        String pass = nvs.getMqttPass();
        String willTopic = _topic + "/status";

        Serial.printf("[MQTT] Konek ke %s...\n", nvs.getMqttHost().c_str());
        bool ok = (user.length() > 0)
            ? _client.connect(clientId.c_str(), user.c_str(), pass.c_str(),
                              willTopic.c_str(), nvs.getMqttQos(), true, "offline")
            : _client.connect(clientId.c_str(), nullptr, nullptr,
                              willTopic.c_str(), nvs.getMqttQos(), true, "offline");

        if (ok) {
            isConnected = true;
            Serial.println("[MQTT] Terhubung!");
            String ts = ntpMgr.getTimeStr();
            logger.add(LOG_MQTT, "Terhubung ke broker", ts.length() ? ts.c_str() : nullptr);

            // Subscribe command topic
            _client.subscribe((_topic + "/cmd").c_str());

            // HA Discovery
            if (!_haDiscover && nvs.getMqttHa()) {
                _publishDiscovery();
                _haDiscover = true;
            }

            delay(100);
            _client.publish(willTopic.c_str(), "online", true);
        } else {
            Serial.printf("[MQTT] Gagal rc=%d\n", _client.state());
        }
    }

    void _publishData() {
        // Battery sensor payload
        {
            JsonDocument doc;
            doc["v"]    = round(battMon.voltage * 100) / 100.0;
            doc["i"]    = round(battMon.current * 1000) / 1000.0;
            doc["p"]    = round(battMon.power   * 100) / 100.0;
            doc["soc"]  = battMon.soc;
            doc["st"]   = battMon.status;
            doc["mahc"] = round(battMon.mahCharge * 10) / 10.0;
            doc["mahd"] = round(battMon.mahDischarge * 10) / 10.0;
            doc["whc"]  = round(battMon.whCharge * 100) / 100.0;
            doc["whd"]  = round(battMon.whDischarge * 100) / 100.0;
            doc["ri"]   = round(battMon.internalR * 10) / 10.0;
            doc["cyc"]  = battMon.cycleCount;
            String out; serializeJson(doc, out);
            _client.publish((_topic + "/sensor/battery").c_str(), out.c_str(), true);
        }

        // Env sensor payload
        if (envSensor.ahtOnline || envSensor.bmpOnline) {
            JsonDocument doc;
            doc["temp"] = round(envSensor.temperature * 10) / 10.0;
            doc["hum"]  = round(envSensor.humidity    * 10) / 10.0;
            doc["pres"] = round(envSensor.pressure    * 10) / 10.0;
            doc["alt"]  = round(envSensor.altitude    * 10) / 10.0;
            doc["wx"]   = envSensor.weatherText;
            String out; serializeJson(doc, out);
            _client.publish((_topic + "/sensor/env").c_str(), out.c_str(), true);
        }

        // Seismic payload
        if (seismic.isOnline) {
            JsonDocument doc;
            doc["mag"] = round(seismic.magnitude * 1000) / 1000.0;
            doc["st"]  = seismic.statusText();
            doc["ax"]  = round(seismic.accX * 1000) / 1000.0;
            doc["ay"]  = round(seismic.accY * 1000) / 1000.0;
            doc["az"]  = round(seismic.accZ * 1000) / 1000.0;
            String out; serializeJson(doc, out);
            _client.publish((_topic + "/sensor/seismic").c_str(), out.c_str(), true);
        }

        // Relay status
        for (int i = 0; i < RELAY_COUNT; i++) {
            String t = _topic + "/relay/" + String(i + 1);
            _client.publish(t.c_str(), relayMgr.state[i] ? "ON" : "OFF", true);
        }
    }

    void _publishDiscovery() {
        String devId = nvs.getDeviceName();
        devId.toLowerCase(); devId.replace(" ", "_");
        String statTopic  = _topic + "/sensor/battery";
        String avtyTopic  = _topic + "/status";

        // Helper lambda-like untuk publish config
        auto pub = [&](const String& sId, JsonDocument& doc) {
            String t = "homeassistant/sensor/" + devId + "/" + sId + "/config";
            String p; serializeJson(doc, p);
            _client.publish(t.c_str(), p.c_str(), true);
            delay(40);
        };

        auto makeDevice = [&](JsonDocument& doc) {
            JsonObject dev = doc["device"].to<JsonObject>();
            dev["name"]         = nvs.getDeviceName();
            dev["identifiers"]  = devId;
            dev["model"]        = "ESP32-C3 BattAnalyzer";
            dev["manufacturer"] = "BattLab";
        };

        // Voltage
        { JsonDocument d; makeDevice(d);
          d["name"]="Battery Voltage"; d["unique_id"]=devId+"_volt";
          d["state_topic"]=statTopic; d["value_template"]="{{ value_json.v }}";
          d["unit_of_measurement"]="V"; d["device_class"]="voltage";
          d["availability_topic"]=avtyTopic;
          pub("voltage", d); }

        // Current
        { JsonDocument d; makeDevice(d);
          d["name"]="Battery Current"; d["unique_id"]=devId+"_curr";
          d["state_topic"]=statTopic; d["value_template"]="{{ value_json.i }}";
          d["unit_of_measurement"]="A"; d["device_class"]="current";
          d["availability_topic"]=avtyTopic;
          pub("current", d); }

        // SoC
        { JsonDocument d; makeDevice(d);
          d["name"]="Battery SoC"; d["unique_id"]=devId+"_soc";
          d["state_topic"]=statTopic; d["value_template"]="{{ value_json.soc }}";
          d["unit_of_measurement"]="%"; d["device_class"]="battery";
          d["availability_topic"]=avtyTopic;
          pub("soc", d); }

        // Temperature (jika ada)
        if (envSensor.ahtOnline || envSensor.bmpOnline) {
            String envTopic = _topic + "/sensor/env";
            { JsonDocument d; makeDevice(d);
              d["name"]="Temperature"; d["unique_id"]=devId+"_temp";
              d["state_topic"]=envTopic; d["value_template"]="{{ value_json.temp }}";
              d["unit_of_measurement"]="°C"; d["device_class"]="temperature";
              d["availability_topic"]=avtyTopic;
              pub("temperature", d); }

            { JsonDocument d; makeDevice(d);
              d["name"]="Humidity"; d["unique_id"]=devId+"_hum";
              d["state_topic"]=envTopic; d["value_template"]="{{ value_json.hum }}";
              d["unit_of_measurement"]="%"; d["device_class"]="humidity";
              d["availability_topic"]=avtyTopic;
              pub("humidity", d); }
        }

        Serial.println("[MQTT] HA Discovery published");
    }

    void _onMessage(char* topic, byte* payload, unsigned int len) {
        String t = String(topic);
        String msg = "";
        for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
        Serial.printf("[MQTT] CMD: %s = %s\n", topic, msg.c_str());

        // relay/1 .. relay/4
        for (int i = 0; i < RELAY_COUNT; i++) {
            if (t.endsWith("/relay/" + String(i + 1))) {
                relayMgr.set(i, msg == "ON" || msg == "1" || msg == "true");
                return;
            }
        }
        if (t.endsWith("/cmd")) {
            if (msg == "restart") ESP.restart();
            if (msg == "relay1_toggle") relayMgr.toggle(0);
        }
    }
};

MQTTManager mqttMgr;
