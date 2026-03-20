#pragma once

// ============================================================
// BattAnalyzer — Config.h
// Semua konstanta, pin assignment, dan default value
// Board: ESP32-C3 Super Mini
// ============================================================

// ── Firmware ─────────────────────────────────────────────────
#define FW_VERSION          "1.0.0"
#define FW_BUILD            __DATE__ " " __TIME__
#define DEVICE_NAME_DEFAULT "BattAnalyzer"

// ── Pin Assignment ────────────────────────────────────────────
// PERHATIAN: GPIO8 = onboard LED (active LOW), GPIO9 = BOOT
// Kedua pin ini TIDAK boleh digunakan untuk fungsi lain!

#define PIN_SDA             10    // I2C Data  — pull-up 4.7kΩ ke 3.3V
#define PIN_SCL             20    // I2C Clock — pull-up 4.7kΩ ke 3.3V

#define PIN_BUZZER          1     // Passive buzzer PWM (active HIGH, 1kΩ seri)
#define PIN_BUTTON          3     // Push button navigasi (INPUT_PULLUP, ke GND)

#define PIN_RELAY_1         0     // Relay 1 — cutoff baterai utama (active HIGH)
#define PIN_RELAY_2         4     // Relay 2 — beban jadwal (active HIGH)
#define PIN_RELAY_3         5     // Relay 3 — beban jadwal (active HIGH)
#define PIN_RELAY_4         6     // Relay 4 — beban jadwal (active HIGH)

#define PIN_ADXL_INT        2     // ADXL345 INT1 — hardware interrupt (RISING)
#define PIN_LED_BOARD       8     // Onboard LED active LOW — JANGAN digunakan

// ── I2C Addresses ─────────────────────────────────────────────
#define ADDR_INA226         0x40
#define ADDR_AHT20          0x38
#define ADDR_BMP280_1       0x76
#define ADDR_BMP280_2       0x77
#define ADDR_ADXL345        0x53
#define ADDR_OLED           0x3C

// ── OLED ──────────────────────────────────────────────────────
#define OLED_WIDTH          128
#define OLED_HEIGHT         32
#define OLED_PAGE_COUNT     7

// ── INA226 ────────────────────────────────────────────────────
#define INA226_SHUNT_OHMS   0.005f    // 5mΩ default
#define INA226_MAX_AMPS     3.2f      // batas arus ±3.2A @ 5mΩ
// Averaging & conversion — gunakan enum langsung di BatteryMonitor.h

// ── Relay ─────────────────────────────────────────────────────
#define RELAY_COUNT         4
#define RELAY_ON            HIGH
#define RELAY_OFF           LOW

// ── Buzzer PWM ────────────────────────────────────────────────
#define BUZZER_CHANNEL      0         // LEDC channel
#define BUZZER_RESOLUTION   8         // 8-bit resolution
#define TONE_STARTUP        1047      // C6
#define TONE_OK             880       // A5
#define TONE_WARN           440       // A4
#define TONE_ALERT          220       // A3
#define TONE_ALARM          110       // A2

// ── WiFi AP ───────────────────────────────────────────────────
#define AP_SSID             "ESP32-BattLab"
#define AP_PASSWORD         ""         // tanpa password agar SSID broadcast
#define AP_IP_STR           "192.168.4.1"
#define WIFI_TIMEOUT_MS     60000UL    // 60 detik timeout koneksi
#define WIFI_RETRY_DELAY    500        // ms antar retry

// ── NVS Keys ──────────────────────────────────────────────────
#define NVS_NS              "battlab"
#define NVS_SETUP_DONE      "setup_done"
#define NVS_WIFI_MODE       "wifi_mode"   // "sta" | "ap"
#define NVS_WIFI_SSID       "wifi_ssid"
#define NVS_WIFI_PASS       "wifi_pass"
#define NVS_DEVICE_NAME     "dev_name"
#define NVS_IP_MODE         "ip_mode"     // "dhcp" | "static"
#define NVS_IP_ADDR         "ip_addr"
#define NVS_IP_GW           "ip_gw"
#define NVS_IP_SN           "ip_sn"
#define NVS_IP_DNS          "ip_dns"
#define NVS_AUTH_USER       "auth_user"
#define NVS_AUTH_PASS       "auth_pass"
#define NVS_NTP_SERVER      "ntp_server"
#define NVS_NTP_OFFSET      "ntp_offset"
#define NVS_NTP_EN          "ntp_en"
#define NVS_MAH_CHARGE      "mah_chg"
#define NVS_MAH_DISCHARGE   "mah_dis"
#define NVS_WH_CHARGE       "wh_chg"
#define NVS_WH_DISCHARGE    "wh_dis"
#define NVS_CYCLE_COUNT     "cycles"
#define NVS_UPTIME          "uptime"
#define NVS_SHUNT_OHMS      "shunt_ohms"
#define NVS_V_MAX           "v_max"
#define NVS_V_CUTOFF        "v_cutoff"
#define NVS_I_MAX           "i_max"
#define NVS_CAP_NOMINAL     "cap_nom"
#define NVS_CUTOFF_EN       "cutoff_en"
#define NVS_OLED_PAGE       "oled_page"
#define NVS_OLED_SCROLL     "oled_scroll"
#define NVS_OLED_DUR        "oled_dur"
#define NVS_OLED_BRIGHT     "oled_bright"
#define NVS_OLED_ON         "oled_on"
#define NVS_SEISMIC_THR     "seis_thr"
#define NVS_SEISMIC_CAL_X   "seis_cx"
#define NVS_SEISMIC_CAL_Y   "seis_cy"
#define NVS_SEISMIC_CAL_Z   "seis_cz"
#define NVS_SEISMIC_CAL_OK  "seis_cal"
#define NVS_LOC_NAME        "loc_name"
#define NVS_LOC_LAT         "loc_lat"
#define NVS_LOC_LNG         "loc_lng"
#define NVS_MQTT_EN         "mqtt_en"
#define NVS_MQTT_HOST       "mqtt_host"
#define NVS_MQTT_PORT       "mqtt_port"
#define NVS_MQTT_USER       "mqtt_user"
#define NVS_MQTT_PASS       "mqtt_pass"
#define NVS_MQTT_TOPIC      "mqtt_topic"
#define NVS_MQTT_INTERVAL   "mqtt_intv"
#define NVS_MQTT_QOS        "mqtt_qos"
#define NVS_MQTT_HA         "mqtt_ha"
#define NVS_TELE_TOKEN      "tele_token"
#define NVS_TELE_CHATID     "tele_cid"
#define NVS_TELE_EN         "tele_en"
#define NVS_AHT_T_OFF       "aht_t_off"
#define NVS_AHT_H_OFF       "aht_h_off"
#define NVS_BMP_T_OFF       "bmp_t_off"
#define NVS_RELAY_EN(n)     ("rly" #n "_en")

// ── Default Values ─────────────────────────────────────────────
#define DEF_AUTH_USER       "admin"
#define DEF_AUTH_PASS       "123123"
#define DEF_NTP_SERVER      "pool.ntp.org"
#define DEF_NTP_OFFSET      7          // WIB UTC+7
#define DEF_V_MAX           4.2f       // Li-Ion single cell
#define DEF_V_CUTOFF        3.0f
#define DEF_I_MAX           3.0f
#define DEF_CAP_NOMINAL     2000.0f    // mAh
#define DEF_CUTOFF_EN       true
#define DEF_SEISMIC_THR     0.5f       // g
#define DEF_MQTT_TOPIC      "battanalyzer"
#define DEF_MQTT_PORT       1883
#define DEF_MQTT_INTERVAL   30
#define DEF_OLED_BRIGHT     128

// ── Timing Intervals ──────────────────────────────────────────
#define INTERVAL_SENSOR_MS  500UL     // baca sensor
#define INTERVAL_ACCUM_MS   10000UL   // akumulasi mAh/Wh
#define INTERVAL_SAVE_MS    300000UL  // simpan NVS (5 menit)
#define INTERVAL_UPTIME_MS  1000UL    // uptime counter
#define INTERVAL_RELAY_MS   60000UL   // cek jadwal relay (1 menit)
#define NTP_SYNC_INTERVAL   (6UL * 3600 * 1000)  // 6 jam

// ── Log ───────────────────────────────────────────────────────
#define LOG_MAX_ENTRIES     200
#define LOG_MSG_LEN         120

// Kategori log
#define LOG_SYSTEM  "SYSTEM"
#define LOG_WIFI    "WIFI"
#define LOG_MQTT    "MQTT"
#define LOG_SENSOR  "SENSOR"
#define LOG_BATT    "BATT"
#define LOG_ALERT   "ALERT"
#define LOG_TELE    "TELE"
#define LOG_OTA     "OTA"