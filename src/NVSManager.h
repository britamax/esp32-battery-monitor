#pragma once

// ============================================================
// BattAnalyzer — NVSManager.h
// Wrapper Preferences NVS — simpan/baca semua konfigurasi
// ============================================================

#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"

class NVSManager {
private:
    Preferences _p;

public:
    void begin() {
        _p.begin(NVS_NS, false);
    }
    void end() { _p.end(); }

    // ── Setup ──────────────────────────────────────────────────
    bool isSetupDone()       { return _p.getBool(NVS_SETUP_DONE, false); }
    void setSetupDone(bool v){ _p.putBool(NVS_SETUP_DONE, v); }

    // ── WiFi ───────────────────────────────────────────────────
    String getWifiMode()     { return _p.getString(NVS_WIFI_MODE, "ap"); }
    String getWifiSSID()     { return _p.getString(NVS_WIFI_SSID, ""); }
    String getWifiPass()     { return _p.getString(NVS_WIFI_PASS, ""); }
    bool   hasWifiCreds()    { return _p.getString(NVS_WIFI_SSID, "").length() > 0; }

    void setWifiMode(String m)          { _p.putString(NVS_WIFI_MODE, m); }
    void setWifiCreds(String s, String p){ _p.putString(NVS_WIFI_SSID, s); _p.putString(NVS_WIFI_PASS, p); }

    // ── IP Static ──────────────────────────────────────────────
    String getIPMode()   { return _p.getString(NVS_IP_MODE, "dhcp"); }
    String getStaticIP() { return _p.getString(NVS_IP_ADDR, ""); }
    String getGateway()  { return _p.getString(NVS_IP_GW,   ""); }
    String getSubnet()   { return _p.getString(NVS_IP_SN,   "255.255.255.0"); }
    String getDNS()      { return _p.getString(NVS_IP_DNS,  "8.8.8.8"); }

    void setIPMode(String m) { _p.putString(NVS_IP_MODE, m); }
    void setStaticIP(String ip, String gw, String sn, String dns) {
        _p.putString(NVS_IP_ADDR, ip);
        _p.putString(NVS_IP_GW,   gw);
        _p.putString(NVS_IP_SN,   sn);
        _p.putString(NVS_IP_DNS,  dns);
    }

    // ── Device ─────────────────────────────────────────────────
    String getDeviceName()          { return _p.getString(NVS_DEVICE_NAME, DEVICE_NAME_DEFAULT); }
    void   setDeviceName(String n)  { _p.putString(NVS_DEVICE_NAME, n); }

    // ── Auth ───────────────────────────────────────────────────
    String getAuthUser()           { return _p.getString(NVS_AUTH_USER, DEF_AUTH_USER); }
    String getAuthPass()           { return _p.getString(NVS_AUTH_PASS, DEF_AUTH_PASS); }
    void   setAuthUser(String u)   { _p.putString(NVS_AUTH_USER, u); }
    void   setAuthPass(String p)   { _p.putString(NVS_AUTH_PASS, p); }
    void   resetAuth()             { _p.remove(NVS_AUTH_USER); _p.remove(NVS_AUTH_PASS); }

    // ── NTP ────────────────────────────────────────────────────
    bool   getNtpEn()              { return _p.getBool(NVS_NTP_EN, true); }
    String getNtpServer()          { return _p.getString(NVS_NTP_SERVER, DEF_NTP_SERVER); }
    int    getNtpOffset()          { return _p.getInt(NVS_NTP_OFFSET, DEF_NTP_OFFSET); }
    void   setNtpEn(bool v)        { _p.putBool(NVS_NTP_EN, v); }
    void   setNtpServer(String s)  { _p.putString(NVS_NTP_SERVER, s); }
    void   setNtpOffset(int v)     { _p.putInt(NVS_NTP_OFFSET, v); }

    // ── Battery Parameters ─────────────────────────────────────
    float getVMax()           { return _p.getFloat(NVS_V_MAX,     DEF_V_MAX); }
    float getVCutoff()        { return _p.getFloat(NVS_V_CUTOFF,  DEF_V_CUTOFF); }
    float getIMax()           { return _p.getFloat(NVS_I_MAX,     DEF_I_MAX); }
    float getCapNominal()     { return _p.getFloat(NVS_CAP_NOMINAL, DEF_CAP_NOMINAL); }
    bool  getCutoffEn()       { return _p.getBool(NVS_CUTOFF_EN,  DEF_CUTOFF_EN); }
    float getShuntOhms()      { return _p.getFloat(NVS_SHUNT_OHMS, INA226_SHUNT_OHMS); }

    void setVMax(float v)         { _p.putFloat(NVS_V_MAX,      v); }
    void setVCutoff(float v)      { _p.putFloat(NVS_V_CUTOFF,   v); }
    void setIMax(float v)         { _p.putFloat(NVS_I_MAX,      v); }
    void setCapNominal(float v)   { _p.putFloat(NVS_CAP_NOMINAL, v); }
    void setCutoffEn(bool v)      { _p.putBool(NVS_CUTOFF_EN,   v); }
    void setShuntOhms(float v)    { _p.putFloat(NVS_SHUNT_OHMS, v); }

    // ── Accumulated Energy ─────────────────────────────────────
    float getMahCharge()    { return _p.getFloat(NVS_MAH_CHARGE,    0.0f); }
    float getMahDischarge() { return _p.getFloat(NVS_MAH_DISCHARGE, 0.0f); }
    float getWhCharge()     { return _p.getFloat(NVS_WH_CHARGE,     0.0f); }
    float getWhDischarge()  { return _p.getFloat(NVS_WH_DISCHARGE,  0.0f); }
    int   getCycleCount()   { return _p.getInt(NVS_CYCLE_COUNT,     0); }

    void saveEnergy(float mahChg, float mahDis, float whChg, float whDis, int cycles) {
        _p.putFloat(NVS_MAH_CHARGE,    mahChg);
        _p.putFloat(NVS_MAH_DISCHARGE, mahDis);
        _p.putFloat(NVS_WH_CHARGE,     whChg);
        _p.putFloat(NVS_WH_DISCHARGE,  whDis);
        _p.putInt(NVS_CYCLE_COUNT,     cycles);
    }
    void resetEnergy() {
        _p.putFloat(NVS_MAH_CHARGE,    0.0f);
        _p.putFloat(NVS_MAH_DISCHARGE, 0.0f);
        _p.putFloat(NVS_WH_CHARGE,     0.0f);
        _p.putFloat(NVS_WH_DISCHARGE,  0.0f);
        _p.putInt(NVS_CYCLE_COUNT,     0);
    }

    // ── Uptime ─────────────────────────────────────────────────
    unsigned long getUptime()          { return _p.getULong(NVS_UPTIME, 0); }
    void          saveUptime(unsigned long s) { _p.putULong(NVS_UPTIME, s); }

    // ── OLED ───────────────────────────────────────────────────
    int  getOledPage()       { return _p.getInt(NVS_OLED_PAGE,  0); }
    bool getOledScroll()     { return _p.getBool(NVS_OLED_SCROLL, false); }
    int  getOledDur()        { return _p.getInt(NVS_OLED_DUR,   3000); }
    int  getOledBright()     { return _p.getInt(NVS_OLED_BRIGHT, DEF_OLED_BRIGHT); }
    bool getOledOn()         { return _p.getBool(NVS_OLED_ON,   true); }

    void setOledPage(int v)       { _p.putInt(NVS_OLED_PAGE,    v); }
    void setOledScroll(bool v)    { _p.putBool(NVS_OLED_SCROLL, v); }
    void setOledDur(int v)        { _p.putInt(NVS_OLED_DUR,     v); }
    void setOledBright(int v)     { _p.putInt(NVS_OLED_BRIGHT,  v); }
    void setOledOn(bool v)        { _p.putBool(NVS_OLED_ON,     v); }

    // ── Seismic ────────────────────────────────────────────────
    float getSeismicThr()    { return _p.getFloat(NVS_SEISMIC_THR, DEF_SEISMIC_THR); }
    bool  isSeismicCal()     { return _p.getBool(NVS_SEISMIC_CAL_OK, false); }
    float getSeismicCalX()   { return _p.getFloat(NVS_SEISMIC_CAL_X, 0.0f); }
    float getSeismicCalY()   { return _p.getFloat(NVS_SEISMIC_CAL_Y, 0.0f); }
    float getSeismicCalZ()   { return _p.getFloat(NVS_SEISMIC_CAL_Z, 1.0f); }

    void setSeismicThr(float v)   { _p.putFloat(NVS_SEISMIC_THR, v); }
    void saveSeismicCal(float x, float y, float z) {
        _p.putFloat(NVS_SEISMIC_CAL_X, x);
        _p.putFloat(NVS_SEISMIC_CAL_Y, y);
        _p.putFloat(NVS_SEISMIC_CAL_Z, z);
        _p.putBool(NVS_SEISMIC_CAL_OK, true);
    }
    void resetSeismicCal() {
        _p.putBool(NVS_SEISMIC_CAL_OK, false);
        _p.remove(NVS_SEISMIC_CAL_X);
        _p.remove(NVS_SEISMIC_CAL_Y);
        _p.remove(NVS_SEISMIC_CAL_Z);
    }

    // ── Location ───────────────────────────────────────────────
    String  getLocName()  { return _p.getString(NVS_LOC_NAME, ""); }
    double  getLocLat()   { return _p.getDouble(NVS_LOC_LAT,  0.0); }
    double  getLocLng()   { return _p.getDouble(NVS_LOC_LNG,  0.0); }
    bool    isLocSet()    { return _p.getString(NVS_LOC_NAME, "").length() > 0; }

    void setLocation(String name, double lat, double lng) {
        _p.putString(NVS_LOC_NAME, name);
        _p.putDouble(NVS_LOC_LAT,  lat);
        _p.putDouble(NVS_LOC_LNG,  lng);
    }

    // ── MQTT ───────────────────────────────────────────────────
    bool   getMqttEn()        { return _p.getBool(NVS_MQTT_EN,   false); }
    String getMqttHost()      { return _p.getString(NVS_MQTT_HOST,  ""); }
    int    getMqttPort()      { return _p.getInt(NVS_MQTT_PORT,   DEF_MQTT_PORT); }
    String getMqttUser()      { return _p.getString(NVS_MQTT_USER,  ""); }
    String getMqttPass()      { return _p.getString(NVS_MQTT_PASS,  ""); }
    String getMqttTopic()     { return _p.getString(NVS_MQTT_TOPIC, DEF_MQTT_TOPIC); }
    int    getMqttInterval()  { return _p.getInt(NVS_MQTT_INTERVAL, DEF_MQTT_INTERVAL); }
    int    getMqttQos()       { return _p.getInt(NVS_MQTT_QOS,    0); }
    bool   getMqttHa()        { return _p.getBool(NVS_MQTT_HA,    true); }

    void setMqtt(bool en, String host, int port, String user, String pass,
                 String topic, int interval, int qos, bool ha) {
        _p.putBool(NVS_MQTT_EN,       en);
        _p.putString(NVS_MQTT_HOST,   host);
        _p.putInt(NVS_MQTT_PORT,      port);
        _p.putString(NVS_MQTT_USER,   user);
        _p.putString(NVS_MQTT_PASS,   pass);
        _p.putString(NVS_MQTT_TOPIC,  topic);
        _p.putInt(NVS_MQTT_INTERVAL,  interval);
        _p.putInt(NVS_MQTT_QOS,       qos);
        _p.putBool(NVS_MQTT_HA,       ha);
    }

    // ── Telegram ───────────────────────────────────────────────
    bool   getTeleEn()        { return _p.getBool(NVS_TELE_EN,    false); }
    String getTeleToken()     { return _p.getString(NVS_TELE_TOKEN, ""); }
    String getTeleChatId()    { return _p.getString(NVS_TELE_CHATID,""); }

    void setTele(bool en, String token, String chatid) {
        _p.putBool(NVS_TELE_EN,        en);
        _p.putString(NVS_TELE_TOKEN,   token);
        _p.putString(NVS_TELE_CHATID,  chatid);
    }

    // ── Env Offsets ────────────────────────────────────────────
    float getAhtTempOffset() { return _p.getFloat(NVS_AHT_T_OFF, 0.0f); }
    float getAhtHumOffset()  { return _p.getFloat(NVS_AHT_H_OFF, 0.0f); }
    float getBmpTempOffset() { return _p.getFloat(NVS_BMP_T_OFF, 0.0f); }
    void setAhtTempOffset(float v) { _p.putFloat(NVS_AHT_T_OFF, v); }
    void setAhtHumOffset(float v)  { _p.putFloat(NVS_AHT_H_OFF, v); }
    void setBmpTempOffset(float v) { _p.putFloat(NVS_BMP_T_OFF, v); }

    // ── Relay Schedule ─────────────────────────────────────────
    // Setiap relay: enabled, on_hour, on_min, off_hour, off_min, day_mask
    bool getRelayEn(int n) {
        char k[12]; snprintf(k, sizeof(k), "rly%d_en", n);
        return _p.getBool(k, false);
    }
    int getRelayOnH(int n)  { char k[12]; snprintf(k,sizeof(k),"rly%d_onh",n); return _p.getInt(k,0); }
    int getRelayOnM(int n)  { char k[12]; snprintf(k,sizeof(k),"rly%d_onm",n); return _p.getInt(k,0); }
    int getRelayOffH(int n) { char k[12]; snprintf(k,sizeof(k),"rly%d_ofh",n); return _p.getInt(k,23); }
    int getRelayOffM(int n) { char k[12]; snprintf(k,sizeof(k),"rly%d_ofm",n); return _p.getInt(k,59); }
    int getRelayDayMask(int n) { char k[12]; snprintf(k,sizeof(k),"rly%d_day",n); return _p.getInt(k,0x7F); }

    void setRelay(int n, bool en, int onh, int onm, int ofh, int ofm, int day) {
        char k[12];
        snprintf(k,sizeof(k),"rly%d_en",n);  _p.putBool(k,en);
        snprintf(k,sizeof(k),"rly%d_onh",n); _p.putInt(k,onh);
        snprintf(k,sizeof(k),"rly%d_onm",n); _p.putInt(k,onm);
        snprintf(k,sizeof(k),"rly%d_ofh",n); _p.putInt(k,ofh);
        snprintf(k,sizeof(k),"rly%d_ofm",n); _p.putInt(k,ofm);
        snprintf(k,sizeof(k),"rly%d_day",n); _p.putInt(k,day);
    }

    // ── Reset ──────────────────────────────────────────────────
    void factoryReset()    { _p.clear(); }
    void clearWifiOnly() {
        _p.remove(NVS_WIFI_SSID); _p.remove(NVS_WIFI_PASS);
        _p.remove(NVS_WIFI_MODE); _p.remove(NVS_SETUP_DONE);
        _p.remove(NVS_IP_MODE);   _p.remove(NVS_IP_ADDR);
        _p.remove(NVS_IP_GW);     _p.remove(NVS_IP_SN);
        _p.remove(NVS_IP_DNS);
    }
};

NVSManager nvs;
