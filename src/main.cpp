#include <Arduino.h>
#include <WiFi.h>
#include "esp_bt.h"
#include "esp_sleep.h"
#include "rg15.h"

// ---------- PINS (KEEP AS YOU REQUESTED) ----------
#define SENSOR_RX_PIN   21   // ESP32 RX  <- RG-15 TX
#define SENSOR_TX_PIN   22   // ESP32 TX  -> RG-15 RX
#define RADIO_RX_PIN    16   // ESP32 RX  <- Radio TX
#define RADIO_TX_PIN    17   // ESP32 TX  -> Radio RX

// ---------- BAUD RATES ----------
#define RG_BAUD     9600
#define RADIO_BAUD  38400

// ---------- SENSOR ID ----------
const char* MY_SENSOR_ID = "RF1";

// ---------- BUFFERS ----------
char rgLine[64];

// ---------- SLEEP SETTINGS ----------
#define uS_TO_S_FACTOR 1000000ULL
#define PERIOD_SECONDS 20ULL  // 1 hour

// ---------------- READ ONE LINE FROM RG-15 ----------------


// ---------------- REQUEST RAIN FROM RG-15 ----------------
bool readRainAccumLine(char *out, size_t maxLen) {
  while (Serial1.available()) Serial1.read();  // flush old bytes
  Serial1.print("A\r\n");
  return readRG15Line(out, maxLen, 1500);
}

// ---------------- SEND TO RADIO ----------------
void sendToRadio(const char *payloadLine) {
  Serial2.print(MY_SENSOR_ID);
  Serial2.print(":");
  Serial2.print(payloadLine);
  Serial2.print("\n");
  Serial2.flush();
}

// ---------------- GO TO DEEP SLEEP ----------------
void goToDeepSleep_1hour() {
  delay(80); // let UART finish
  esp_sleep_enable_timer_wakeup(PERIOD_SECONDS * uS_TO_S_FACTOR);
  Serial.println("Going to deep sleep for 1 hour...");
  Serial.flush();
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // ----------- POWER SAVERS -----------
  WiFi.mode(WIFI_OFF);
  btStop();
  esp_bt_controller_disable();
  setCpuFrequencyMhz(80);

  // RG-15 UART
  Serial1.begin(RG_BAUD, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);

  // UART to Meshtastic radio
  Serial2.begin(RADIO_BAUD, SERIAL_8N1, RADIO_RX_PIN, RADIO_TX_PIN);

  delay(400); // allow everything to settle after boot

  Serial.println("Periodic RG-15 reader (double-read, discard first)");

  // ---- 1st read (discard) ----
  char dummy[64];
  bool ok1 = readRainAccumLine(dummy, sizeof(dummy));
  Serial.print("Discarded 1st read: ");
  Serial.println(ok1 ? dummy : "TIMEOUT");

  delay(250); // short gap before second read

  // ---- 2nd read (send) ----
  bool ok2 = readRainAccumLine(rgLine, sizeof(rgLine));
  if (ok2) {
    sendToRadio(rgLine);
    Serial.print("Sent 2nd read: ");
    Serial.println(rgLine);
  } else {
    sendToRadio("ERR");
    Serial.println("Sent: ERR (2nd read timeout)");
  }

  goToDeepSleep_1hour();
}

void loop() {
  // never reached
}
