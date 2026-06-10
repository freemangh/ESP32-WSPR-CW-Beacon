#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <JTEncode.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>

#include "Config.h"
#include "Radio.h"

// Objects
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
JTEncode jtEncode;

// Global State
bool wifiConnected = false;
bool timeSynced = false;
bool isTransmittingWSPR = false;
bool isTransmittingCW = false;
bool isTransmittingFT8 = false;

// WSPR State
uint8_t tx_buffer[255]; // Encoded symbols (values 0-3)
int symbol_count = 0;
int current_symbol_index = 0;
unsigned long time_last_symbol = 0;
const unsigned long WSPR_SYMBOL_DURATION_US = 682667;
const double WSPR_TONE_SPACING = 1.4648;

// FT8 State
uint8_t tx_buffer_ft8[79];
int ft8_symbol_count = 0;
int current_ft8_index = 0;
unsigned long time_last_ft8 = 0;
const unsigned long FT8_SYMBOL_DURATION_US = 160000;
const double FT8_TONE_SPACING_VAL = 6.25;

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

// Epoch values below this mean the system clock has not been set yet
const time_t TIME_VALID_EPOCH = 1600000000; // 2020-09-13

String formatUTC() {
  time_t now = time(nullptr);
  struct tm tmv;
  gmtime_r(&now, &tmv);
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
           tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
           tmv.tm_min, tmv.tm_sec);
  return String(buf);
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
  display.println("ESP32 Beacon");
  display.display();
}

void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  display.println("Connecting WiFi...");
  display.display();

  WiFi.setAutoReconnect(true); // reconnect in the background if the AP drops
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

  // Built-in SNTP: fully asynchronous, resyncs in the background (hourly by
  // default) and never blocks loop() — a failed NTP poll must not stall the
  // WSPR/FT8 symbol pacing.
  configTime(UTC_OFFSET * 3600, 0, NTP_SERVER);

  // Wait up to 10 s for the first sync; if it doesn't land, SNTP keeps
  // trying in the background and loop() picks it up when it arrives.
  for (int i = 0; i < 100 && time(nullptr) < TIME_VALID_EPOCH; i++)
    delay(100);

  if (time(nullptr) >= TIME_VALID_EPOCH) {
    timeSynced = true;
    Serial.println("Time Synced: " + formatUTC());
    display.println("Time Synced!");
  } else {
    Serial.println("NTP sync pending (will retry in background)");
    display.println("NTP pending...");
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

void prepareFT8() {
  Serial.println("Encoding FT8 message...");
  memset(tx_buffer_ft8, 0, sizeof(tx_buffer_ft8));
  // Format: CQ [CALL] [GRID4]
  char msg[20];
  // FT8 Grid is 4 chars. Remove last 2 chars from 6-char grid if needed.
  char grid4[5];
  strncpy(grid4, GRID_LOCATOR, 4);
  grid4[4] = '\0';

  snprintf(msg, sizeof(msg), "CQ %s %s", CALLSIGN, grid4);

  jtEncode.ft8_encode(msg, tx_buffer_ft8);
  ft8_symbol_count = 79;
  Serial.print("FT8 Symbols generated: ");
  Serial.println(ft8_symbol_count);
}

void setup() {
  Serial.begin(115200);
  setupDisplay();
  connectWiFi();
  syncTime();

  Radio::begin();
  if (ENABLE_WSPR)
    prepareWSPR();
  if (ENABLE_FT8)
    prepareFT8();
}

// --- WSPR Logic ---
void startWSPR() {
  isTransmittingWSPR = true;
  current_symbol_index = 0;
  time_last_symbol = micros();
  Serial.println("Starting WSPR TX");
  Radio::setFrequency(
      WSPR_FREQ_20M); // Base freq. Actual transmission adds tones.
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
      double tone_freq = WSPR_FREQ_20M + (symbol * WSPR_TONE_SPACING);
      Radio::setFrequency(tone_freq);
      current_symbol_index++;
    } else {
      stopWSPR();
    }
  }
}

// --- FT8 Logic ---
void startFT8() {
  isTransmittingFT8 = true;
  current_ft8_index = 0;
  time_last_ft8 = micros();
  Serial.println("Starting FT8 TX");
  // Set initial freq (Base + Offset + Symbol 0 of GFSK)
  // Actually, we update it in the loop immediately, but setting it here primes
  // the PLL
  Radio::setFrequency(FT8_FREQ_20M + FT8_OFFSET_HZ);
}

void stopFT8() {
  isTransmittingFT8 = false;
  Radio::disable();
  Serial.println("Stop FT8 TX");
}

void updateFT8() {
  if (!isTransmittingFT8)
    return;

  unsigned long now = micros();
  if (now - time_last_ft8 >= FT8_SYMBOL_DURATION_US) {
    time_last_ft8 += FT8_SYMBOL_DURATION_US;

    if (current_ft8_index < ft8_symbol_count) {
      uint8_t symbol = tx_buffer_ft8[current_ft8_index];
      // FT8 uses 8-GFSK tones spaced by 6.25Hz
      double tone_freq =
          (FT8_FREQ_20M + FT8_OFFSET_HZ) + (symbol * FT8_TONE_SPACING_VAL);
      Radio::setFrequency(tone_freq);
      current_ft8_index++;
    } else {
      stopFT8();
    }
  }
}

// --- CW Logic ---
void startCW() {
  isTransmittingCW = true;
  cw_char_index = -1; // updateCW pre-increments before reading a char
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
        // Reset element index for next char
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
    // Transmit CW on WSPR freq (or close to it)
    Radio::setFrequency(WSPR_FREQ_20M);
    cw_state = CW_TONE;
    if (element == '.') {
      next_event_time = now + CW_DOT_MS;
    } else {
      next_event_time = now + (3 * CW_DOT_MS);
    }
  }
}

void loop() {
  // SNTP keeps the clock fresh in the background; just notice the first sync
  // in case it wasn't ready during setup
  if (!timeSynced && time(nullptr) >= TIME_VALID_EPOCH) {
    timeSynced = true;
    Serial.println("Time Synced: " + formatUTC());
  }

  // Scheduler
  // Schedule Cycle (4 minutes):
  // Min 0: WSPR Start (Even)
  // Min 1: WSPR Cont/End
  // Min 2: FT8 (4 slots: 00, 15, 30, 45)
  // Min 3: CW ID

  if (timeSynced && !isTransmittingWSPR && !isTransmittingCW &&
      !isTransmittingFT8) {
    time_t rawTime = time(nullptr);
    struct tm *ptm = gmtime(&rawTime);

    int currentMinute = ptm->tm_min;
    int currentSecond = ptm->tm_sec;
    int minuteInCycle = currentMinute % 4; // 0, 1, 2, 3

    // WSPR: Minute 0-1 (Start at 00:01)
    if (ENABLE_WSPR && minuteInCycle == 0 && currentSecond == 1) {
      startWSPR();
    }

    // FT8: Minute 2 (Slots: 00, 15, 30, 45)
    else if (ENABLE_FT8 && minuteInCycle == 2) {
      // Start at 01, 16, 31, 46 to allow 1s settling logic if needed, or stick
      // to 00 FT8 standard starts at :00, :15, :30, :45. We use :01, :16 etc
      // simply to ensure we didn't miss the second tick or conflict with loop
      // timing
      if (currentSecond == 1 || currentSecond == 16 || currentSecond == 31 ||
          currentSecond == 46) {
        startFT8();
      }
    }

    // CW: Minute 3 (Start at 00:10)
    else if (ENABLE_CW && minuteInCycle == 3 && currentSecond == 10) {
      startCW();
    }
  }

  if (isTransmittingWSPR)
    updateWSPR();
  if (isTransmittingFT8)
    updateFT8();
  if (isTransmittingCW)
    updateCW();

  // UI Update Loop (Low priority)
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("ESP32 Beacon");
    display.println(formatUTC()); // UTC date + time, 19 chars fits one line

    // Serial Monitor Output (Headless Mode)
    Serial.print("[");
    Serial.print(formatUTC());
    Serial.print("] ");

    if (isTransmittingWSPR) {
      display.println(">> TX WSPR <<");
      display.print("Sym: ");
      display.print(current_symbol_index);
      display.print("/");
      display.println(symbol_count);

      Serial.print("TX WSPR | Sym: ");
      Serial.print(current_symbol_index);
      Serial.print("/");
      Serial.println(symbol_count);

    } else if (isTransmittingFT8) {
      display.println(">> TX FT8 <<");
      display.print("Sym: ");
      display.print(current_ft8_index);
      display.print("/");
      display.println(ft8_symbol_count);

      Serial.print("TX FT8 | Sym: ");
      Serial.print(current_ft8_index);
      Serial.print("/");
      Serial.println(ft8_symbol_count);

    } else if (isTransmittingCW) {
      display.println(">> TX CW <<");
      display.print("Msg: ");
      display.println(cw_message);

      Serial.print("TX CW | Msg: ");
      Serial.println(cw_message);

    } else {
      display.println(">> IDLE <<");

      // Find the next enabled TX event in the 4-minute cycle
      struct TxEvent {
        bool enabled;
        int second; // start offset within the cycle
        const char *name;
      };
      static const TxEvent events[] = {
          {ENABLE_WSPR, 0 * 60 + 1, "WSPR"}, {ENABLE_FT8, 2 * 60 + 1, "FT8"},
          {ENABLE_FT8, 2 * 60 + 16, "FT8"},  {ENABLE_FT8, 2 * 60 + 31, "FT8"},
          {ENABLE_FT8, 2 * 60 + 46, "FT8"},  {ENABLE_CW, 3 * 60 + 10, "CW"},
      };

      time_t rawTime = time(nullptr);
      struct tm *ptm = gmtime(&rawTime);
      int nowInCycle = (ptm->tm_min % 4) * 60 + ptm->tm_sec;

      const char *nextName = nullptr;
      int nextIn = 0;
      for (auto &e : events) {
        if (!e.enabled)
          continue;
        int wait = e.second - nowInCycle;
        if (wait <= 0)
          wait += 4 * 60;
        if (!nextName || wait < nextIn) {
          nextName = e.name;
          nextIn = wait;
        }
      }

      if (nextName) {
        display.print("Next: ");
        display.print(nextName);
        display.print(" in ");
        display.print(nextIn);
        display.print("s");

        Serial.print("IDLE | Next: ");
        Serial.print(nextName);
        Serial.print(" in ");
        Serial.print(nextIn);
        Serial.println("s");
      } else {
        display.print("All modes disabled");
        Serial.println("IDLE | All modes disabled");
      }
    }

    display.display();
  }
}
