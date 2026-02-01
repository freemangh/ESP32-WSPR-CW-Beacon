#pragma once

#include <Arduino.h>

// WiFi Configuration
// TODO: Move to WiFiManager or Web Interface
#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASS "YOUR_PASSWORD"

// Beacon Configuration
#define CALLSIGN "N0CALL"
#define GRID_LOCATOR "AA00aa"
#define TX_POWER_DBM                                                           \
  10 // Not actually used for hardware control yet, just for WSPR encoding

// Frequency Configuration (WSPR Band: 20m)
#define WSPR_FREQ_20M 14095600ULL
#define WSPR_FREQ_40M 7038600ULL
#define TARGET_FREQ WSPR_FREQ_20M

// Hardware Configuration
#define RF_PIN 27 // Safe pin, avoiding Boot (0) or TX/RX (1/3)
#define I2C_SDA 21
#define I2C_SCL 22
#define OLED_RESET -1
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// NTP Configuration
#define NTP_SERVER "pool.ntp.org"
#define UTC_OFFSET 0
