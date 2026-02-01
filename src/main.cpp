#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <JTEncode.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>

#include "Config.h"
#include "Radio.h"

// Objects
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_SERVER, UTC_OFFSET * 3600);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
JTEncode jtEncode;

// Global State
bool wifiConnected = false;
bool timeSynced = false;
bool isTransmittingWSPR = false;
bool isTransmittingCW = false;

// WSPR State
uint8_t tx_buffer[255]; // Encoded symbols (values 0-3)
int symbol_count = 0;
int current_symbol_index = 0;
unsigned long time_last_symbol = 0;
const unsigned long WSPR_SYMBOL_DURATION_US = 682667;
const double WSPR_TONE_SPACING = 1.4648;

// CW State
const char *cw_message = CALLSIGN " " GRID_LOCATOR;
int cw_char_index = 0;
int cw_element_index = 0; // Index within the morse string for a char
unsigned long cw_last_element_time = 0;
const int CW_WPM = 20;
const int CW_DOT_MS = 1200 / CW_WPM;
bool cw_tone_on = false;
String current_morse_char = "";

// Morse Code Table (Simple A-Z, 0-9)
const char *morse_table[] = {
    ".-",    // A
    "-...",  // B
    "-.-.",  // C
    "-..",   // D
    ".",     // E
    "..-.",  // F
    "--.",   // G
    "....",  // H
    "..",    // I
    ".---",  // J
    "-.-",   // K
    ".-..",  // L
    "--",    // M
    "-.",    // N
    "---",   // O
    ".--.",  // P
    "--.-",  // Q
    ".-.",   // R
    "...",   // S
    "-",     // T
    "..-",   // U
    "...-",  // V
    ".--",   // W
    "-..-",  // X
    "-.--",  // Y
    "--..",  // Z
    "-----", // 0
    ".----", // 1
    "..---", // 2
    "...--", // 3
    "....-", // 4
    ".....", // 5
    "-....", // 6
    "--...", // 7
    "---..", // 8
    "----."  // 9
};

const char *getMorse(char c) {
  if (c >= 'a' && c <= 'z')
    c -= 32; // To Upper
  if (c >= 'A' && c <= 'Z')
    return morse_table[c - 'A'];
  if (c >= '0' && c <= '9')
    return morse_table[c - '0' + 26];
  return ""; // Space or unknown
}

void setupDisplay() {
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("ESP32 WSPR Beacon");
  display.display();
}

void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi Connected");
    Serial.println(WiFi.localIP());
    display.println("WiFi Connected!");
  } else {
    Serial.println("\nWiFi Failed");
    display.println("WiFi Failed :(");
  }
  display.display();
}

void syncTime() {
  if (!wifiConnected)
    return;

  Serial.println("Syncing Time...");
  display.println("Syncing NTP...");
  display.display();

  timeClient.begin();
  if (timeClient.update()) {
    timeSynced = true;
    Serial.println("Time Synced: " + timeClient.getFormattedTime());
    display.println("Time Synced!");
  } else {
    Serial.println("NTP Sync Failed");
    display.println("NTP Sync Failed");
  }
  display.display();
}

void prepareWSPR() {
  Serial.println("Encoding WSPR message...");
  memset(tx_buffer, 0, sizeof(tx_buffer));
  jtEncode.wspr_encode(CALLSIGN, GRID_LOCATOR, TX_POWER_DBM, tx_buffer);
  symbol_count = 162;
  Serial.print("Symbols generated: ");
  Serial.println(symbol_count);
}

void setup() {
  Serial.begin(115200);
  setupDisplay();
  connectWiFi();
  syncTime();

  Radio::begin();
  prepareWSPR();
}

// --- WSPR Logic ---
void startWSPR() {
  isTransmittingWSPR = true;
  current_symbol_index = 0;
  time_last_symbol = micros();
  Serial.println("Starting WSPR TX");
  Radio::setFrequency(TARGET_FREQ);
}

void stopWSPR() {
  isTransmittingWSPR = false;
  Radio::disable();
  Serial.println("Stop WSPR TX");
}

void updateWSPR() {
  if (!isTransmittingWSPR)
    return;

  unsigned long now = micros();
  if (now - time_last_symbol >= WSPR_SYMBOL_DURATION_US) {
    time_last_symbol += WSPR_SYMBOL_DURATION_US;

    if (current_symbol_index < symbol_count) {
      uint8_t symbol = tx_buffer[current_symbol_index];
      double tone_freq = TARGET_FREQ + (symbol * WSPR_TONE_SPACING);
      Radio::setFrequency(tone_freq);
      current_symbol_index++;
    } else {
      stopWSPR();
    }
  }
}

// --- CW Logic ---
void startCW() {
  isTransmittingCW = true;
  cw_char_index = 0;
  cw_element_index = 0;
  cw_last_element_time = millis();
  cw_tone_on = false;
  current_morse_char = ""; // Will trigger load of first char
  Serial.println("Starting CW ID");
}

void stopCW() {
  isTransmittingCW = false;
  Radio::disable();
  Serial.println("Stop CW ID");
}

void updateCW() {
  if (!isTransmittingCW)
    return;

  unsigned long now = millis();
  // Simple state machine for CW timing
  // This is blocking-free but simplistic

  // Duration Logic:
  // Dot: 1 unit
  // Dash: 3 units
  // Element space: 1 unit
  // Char space: 3 units
  // Word space: 7 units

  // We need a complex state machine here or a simpler blocking one?
  // Non-blocking is preferred.

  // Simplification: We only process transitions when the current "event" is
  // done.

  static unsigned long next_event_time = 0;
  static enum { CW_IDLE, CW_TONE, CW_SPACE } cw_state = CW_IDLE;

  if (now < next_event_time)
    return; // Wait

  // Event finished, what next?
  if (cw_state == CW_TONE) {
    Radio::disable();
    cw_state = CW_SPACE;
    next_event_time = now + CW_DOT_MS; // Intra-char space (1 unit)
    return;
  }

  // If we were in space (or idle), get next element
  if (cw_state == CW_SPACE || cw_state == CW_IDLE) {
    // Check if we need a new char
    if (cw_element_index >= current_morse_char.length()) {
      // Next char
      cw_char_index++;
      if (cw_char_index >= strlen(cw_message)) {
        stopCW();
        return;
      }

      char c = cw_message[cw_char_index];
      if (c == ' ') {
        // Word space (7 units). We already did 1 unit space after last char
        // keyup. So wait 6 more.
        cw_state = CW_SPACE;
        // Reset element index for next char (which will happen after this wait)
        cw_element_index = 0;
        current_morse_char = ""; // Mark as space
        next_event_time = now + (6 * CW_DOT_MS);
        return;
      } else {
        current_morse_char = String(getMorse(c));
        cw_element_index = 0;
        // Char space (3 units). We already did 1 unit. So wait 2 more.
        cw_state = CW_SPACE;
        next_event_time = now + (2 * CW_DOT_MS);
        return;
      }
    }

    // Output next element of current char
    char element = current_morse_char[cw_element_index++];
    Radio::setFrequency(TARGET_FREQ);
    cw_state = CW_TONE;
    if (element == '.') {
      next_event_time = now + CW_DOT_MS;
    } else {
      next_event_time = now + (3 * CW_DOT_MS);
    }
  }
}

void loop() {
  // Keep time updated
  if (wifiConnected) {
    timeClient.update();
  }

  // Scheduler
  if (timeSynced && !isTransmittingWSPR && !isTransmittingCW) {
    time_t rawTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime(&rawTime);

    int currentMinute = ptm->tm_min;
    int currentSecond = ptm->tm_sec;

    // WSPR: Even minutes :01
    if (currentMinute % 2 == 0 && currentSecond == 1) {
      startWSPR();
    }
    // CW: Odd minutes :10 (Every 10 mins? Or just every odd minute for now is
    // fun) Let's do every odd minute at :10 seconds
    else if (currentMinute % 2 != 0 && currentSecond == 10) {
      startCW();
    }
  }

  if (isTransmittingWSPR)
    updateWSPR();
  if (isTransmittingCW)
    updateCW();

  // UI Update Loop (Low priority)
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("ESP32 WSPR Beacon");
    display.print("Time: ");
    display.println(timeClient.getFormattedTime());

    if (isTransmittingWSPR) {
      display.println(">> TX WSPR <<");
      display.print("Sym: ");
      display.print(current_symbol_index);
      display.print("/");
      display.println(symbol_count);
    } else if (isTransmittingCW) {
      display.println(">> TX CW <<");
      display.print("Msg: ");
      display.println(cw_message);
    } else {
      display.println(">> IDLE <<");
      display.print("Next: TX Cycle");
    }

    display.display();
  }
}
