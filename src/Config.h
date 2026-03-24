#pragma once

// ============================================================
// BattAnalyzer — Config.h
// Semua konstanta, pin assignment, dan default value
// Board: ESP32-C3 Super Mini
// ============================================================

// ── Firmware ─────────────────────────────────────────────────
#define FW_VERSION          "1.1.0"
#define FW_BUILD            __DATE__ " " __TIME__
#define DEVICE_NAME_DEFAULT "BattAnalyzer"

// ── Pin Assignment ────────────────────────────────────────────
// PERHATIAN: GPIO8 = onboard LED (active LOW), GPIO9 = BOOT
// Kedua pin ini TIDAK boleh digunakan untuk fungsi lain!

#define PIN_SDA             10    // I2C Data  — pull-up 4.7kΩ ke 3.3V
#define PIN_SCL             20    // I2C Clock — pull-up 4.7kΩ ke 3.3V

#define PIN_BUZZER          1     // Passive buzzer module KY-012 (active LOW trigger)
#define PIN_BUTTON          3     // Push button navigasi (INPUT_PULLUP, ke GND)

// Relay module SRD-5VDC-SL-C — Active LOW Control Signal
// R1=beban, R2=cutoff baterai, R3=beban, R4=beban
#define PIN_RELAY_1         0     // Relay 1 — beban (GPIO0, strapping pin, sesaat LOW saat boot)
#define PIN_RELAY_2         4     // Relay 2 — cutoff baterai otomatis (GPIO4, aman)
#define PIN_RELAY_3         5     // Relay 3 — beban jadwal
#define PIN_RELAY_4         6     // Relay 4 — beban jadwal
#define PIN_RELAY_CUTOFF    PIN_RELAY_2  // alias cutoff
#define IDX_RELAY_CUTOFF    1            // index array (0-based)

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
// Upgrade ke SSD1306 0.96" 128×64
#define OLED_WIDTH          128
#define OLED_HEIGHT         64    // upgrade dari 32 ke 64
#define OLED_PAGE_COUNT     8     // tambah 1 halaman karena lebih besar

// ── INA226 ────────────────────────────────────────────────────
#define INA226_SHUNT_OHMS   0.005f    // 5mΩ default (shunt 2512 2W)
#define INA226_MAX_AMPS     8.0f      // headroom cukup @ 5mΩ

// ── Relay ─────────────────────────────────────────────────────
#define RELAY_COUNT         4
// Modul relay driver S8550 (PNP transistor)
// Masalah: ESP32 HIGH(3.3V) vs VCC(5V) → selisih 1.7V → transistor bocor
// Solusi: High-Z trick di RelayManager.h
//   ON  = pinMode OUTPUT + digitalWrite LOW
//   OFF = pinMode INPUT (floating/High-Z)
// Konstanta di bawah tidak dipakai langsung — lihat RelayManager.h
#define RELAY_ON            LOW
#define RELAY_OFF           HIGH

// ── Buzzer PWM ────────────────────────────────────────────────
// Modul KY-012 / FC-04: Active LOW trigger
// I/O=LOW  → transistor aktif → buzzer bunyi
// I/O=HIGH → transistor off   → buzzer diam
#define BUZZER_CHANNEL      0         // LEDC channel
#define BUZZER_RESOLUTION   8         // 8-bit resolution
#define BUZZER_ACTIVE_LOW   1         // modul active low trigger
// duty cycle 128 = 50% → output rata-rata ~LOW → buzzer bunyi
// duty cycle 0   = 0%  → output HIGH → buzzer diam
#define BUZZER_DUTY_ON      128       // duty saat bunyi (50% PWM)
#define BUZZER_DUTY_OFF     0         // duty saat diam

// Frekuensi nada
#define TONE_STARTUP        1047      // C6
#define TONE_OK             880       // A5
#define TONE_WARN           440       // A4
#define TONE_ALERT          220       // A3
#define TONE_ALARM          110       // A2

// ── WiFi AP ───────────────────────────────────────────────────
#define AP_SSID             "ESP32-BattLab"
#define AP_PASSWORD         ""
#define AP_IP_STR           "192.168.4.1"
#define WIFI_TIMEOUT_MS     60000UL
#define WIFI_RETRY_DELAY    500

// ── NVS Keys ──────────────────────────────────────────────────
#define NVS_NS              "battlab"
#define NVS_SETUP_DONE      "setup_done"
#define NVS_WIFI_MODE       "wifi_mode"
#define NVS_WIFI_SSID       "wifi_ssid"
#define NVS_WIFI_PASS       "wifi_pass"
#define NVS_DEVICE_NAME     "dev_name"
#define NVS_IP_MODE         "ip_mode"
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
#define NVS_V_NOM           "v_nom"       // tegangan nominal (SoC 50% referensi)
#define NVS_V_CUTOFF        "v_cutoff"
#define NVS_I_MAX           "i_max"
#define NVS_CAP_NOMINAL     "cap_nom"
#define NVS_CUTOFF_EN       "cutoff_en"
#define NVS_BATT_TYPE       "batt_type"   // "liion"|"lifepo4"|"lead"|"nimh"|"custom"
#define NVS_BATT_CELLS      "batt_cells"  // jumlah cell seri (S)
#define NVS_SOC_METHOD      "soc_method"  // 0=voltage, 1=coulomb, 2=hybrid
#define NVS_SOC_INIT        "soc_init"    // SoC awal untuk coulomb counting
#define NVS_CAP_REAL        "cap_real"    // kapasitas real terukur (mAh)
#define NVS_SOH             "soh"         // state of health terakhir (%)
#define NVS_INA_AVG         "ina_avg"     // averaging samples index
#define NVS_INA_VCONV       "ina_vconv"   // voltage conversion time index
#define NVS_INA_ICONV       "ina_iconv"   // current conversion time index
#define NVS_INA_MODE        "ina_mode"    // operating mode index
#define NVS_INA_I_OFFSET    "ina_ioff"    // arus offset kalibrasi (A)
#define NVS_INA_V_OFFSET    "ina_voff"    // tegangan offset kalibrasi (V)
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
#define NVS_MQTT_MODE       "mqtt_mode"
#define NVS_MQTT_WS_PATH    "mqtt_wspath"
#define NVS_TELE_TOKEN      "tele_token"
#define NVS_TELE_CHATID     "tele_cid"
#define NVS_TELE_EN         "tele_en"
#define NVS_AHT_T_OFF       "aht_t_off"
#define NVS_AHT_H_OFF       "aht_h_off"
#define NVS_BMP_T_OFF       "bmp_t_off"
#define NVS_RELAY_EN(n)     ("rly" #n "_en")

// ── Alarm Config NVS Keys ─────────────────────────────────────
// Per event: en=aktif, buzz=pola buzzer(0=off), tele=telegram, mqtt=mqtt, cool=cooldown menit
// Event: 0=VOLT_LOW, 1=VOLT_HIGH, 2=OVERCURR, 3=GEMPA, 4=TEMP_HIGH
#define NVS_ALM_EN(n)       // gunakan getAlarmEn(n) di NVSManager
#define ALARM_COUNT         5
#define ALARM_VOLT_LOW      0
#define ALARM_VOLT_HIGH     1
#define ALARM_OVERCURR      2
#define ALARM_QUAKE         3
#define ALARM_TEMP_HIGH     4

// ── History 7 Hari ────────────────────────────────────────────
// 7 slot float untuk charge dan discharge harian (mAh dan Wh)
#define HISTORY_DAYS        7
#define NVS_HIST_CHG_MAH    "h_chg_mah"   // float array 7 × 4 = 28 bytes
#define NVS_HIST_DIS_MAH    "h_dis_mah"
#define NVS_HIST_CHG_WH     "h_chg_wh"
#define NVS_HIST_DIS_WH     "h_dis_wh"
#define NVS_HIST_DAY        "h_day"        // hari terakhir (0-6 NTP wday)
#define NVS_HIST_DATE       "h_date"       // tanggal terakhir (unix day)

// ── Default Values ─────────────────────────────────────────────
#define DEF_AUTH_USER       "admin"
#define DEF_AUTH_PASS       "123123"
#define DEF_NTP_SERVER      "pool.ntp.org"
#define DEF_NTP_OFFSET      7
// Battery defaults — LiFePO4 4S
#define DEF_BATT_TYPE       "lifepo4"
#define DEF_BATT_CELLS      4
#define DEF_V_MAX           14.6f     // LiFePO4 4S full
#define DEF_V_NOM           12.8f     // LiFePO4 4S nominal
#define DEF_V_CUTOFF        11.2f     // LiFePO4 4S cutoff
#define DEF_I_MAX           10.0f
#define DEF_CAP_NOMINAL     4000.0f   // mAh (2P × 2000mAh)
#define DEF_CUTOFF_EN       true
#define DEF_SOC_METHOD      2         // hybrid default
#define DEF_SEISMIC_THR     0.5f
#define DEF_MQTT_TOPIC      "battanalyzer"
#define DEF_MQTT_PORT       1883
#define DEF_MQTT_MODE       "tcp"
#define DEF_MQTT_WS_PATH    "/"
#define DEF_MQTT_INTERVAL   30
#define DEF_OLED_BRIGHT     128
// INA226 defaults
#define DEF_INA_AVG         3         // INA226_AVERAGE_16 (index 3)
#define DEF_INA_VCONV       4         // INA226_CONV_TIME_1100 (index 4)
#define DEF_INA_ICONV       4
#define DEF_INA_MODE        0         // INA226_CONTINUOUS (index 0)

// ── Timing Intervals ──────────────────────────────────────────
#define INTERVAL_SENSOR_MS  500UL
#define INTERVAL_ACCUM_MS   10000UL
#define INTERVAL_SAVE_MS    300000UL
#define INTERVAL_UPTIME_MS  1000UL
#define INTERVAL_RELAY_MS   60000UL
#define NTP_SYNC_INTERVAL   (6UL * 3600 * 1000)

// ── Log ───────────────────────────────────────────────────────
#define LOG_MAX_ENTRIES     200
#define LOG_MSG_LEN         120

#define LOG_SYSTEM  "SYSTEM"
#define LOG_WIFI    "WIFI"
#define LOG_MQTT    "MQTT"
#define LOG_SENSOR  "SENSOR"
#define LOG_BATT    "BATT"
#define LOG_ALERT   "ALERT"
#define LOG_TELE    "TELE"
#define LOG_OTA     "OTA"
#define LOG_DEBUG   "DEBUG"

#define LOG_MASK_SYSTEM   (1 << 0)
#define LOG_MASK_WIFI     (1 << 1)
#define LOG_MASK_MQTT     (1 << 2)
#define LOG_MASK_SENSOR   (1 << 3)
#define LOG_MASK_BATT     (1 << 4)
#define LOG_MASK_ALERT    (1 << 5)
#define LOG_MASK_TELE     (1 << 6)
#define LOG_MASK_OTA      (1 << 7)
#define LOG_MASK_DEBUG    (1 << 8)
#define LOG_MASK_ALL      0x1FF
#define LOG_MASK_DEFAULT  0x0FF
#define NVS_LOG_MASK      "log_mask"

// ── Daily Report & Reset ──────────────────────────────────────
#define NVS_DAILY_RESET_EN  "daily_rst_en"  // bool: reset akumulasi tiap hari
#define NVS_DAILY_REPORT_EN "daily_rpt_en"  // bool: kirim laporan via Telegram
#define NVS_DAILY_TIME_MODE "daily_tm_mode" // 0=jam tetap, 1=sunrise
#define NVS_DAILY_HOUR      "daily_hour"    // jam reset jika mode=0 (default 6)
#define NVS_DAILY_MIN       "daily_min"     // menit reset jika mode=0 (default 0)

// ── Relay Quake Trigger ───────────────────────────────────────
// Per relay: trigger mode 0=manual, 1=jadwal, 2=gempa
// rly1..4_trig, rly1..4_qdur (durasi gempa detik)