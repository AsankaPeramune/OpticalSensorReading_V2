#include <Arduino.h>
#include <WiFi.h>
#include "esp_bt.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "rg15.h"

// ---------------- WAKE PIN (RAK19007 AIN1 -> ESP32) ----------------
#define WAKE_PIN GPIO_NUM_33   // connect AIN1 here (RTC pin)

// ---------------- PINS (KEEP AS YOU REQUESTED) ----------------
#define SENSOR_RX_PIN   21   // ESP32 RX  <- RG-15 TX
#define SENSOR_TX_PIN   22   // ESP32 TX  -> RG-15 RX
#define RADIO_RX_PIN    16   // ESP32 RX  <- Radio TX
#define RADIO_TX_PIN    17   // ESP32 TX  -> Radio RX

// ---------------- BAUD RATES ----------------
#define RG_BAUD     9600
#define RADIO_BAUD  38400

// ---------------- SENSOR ID ----------------
const char* MY_SENSOR_ID = "RF1";

// ---------------- BUFFERS ----------------
char radioBuf[96];
uint8_t radioIdx = 0;
char rgLine[64];

// ---------------- TIMING ----------------
// How long to listen for a command after waking (tune as needed)
static const uint32_t ACTIVE_WINDOW_MS = 12000;
// After sending reply, keep awake briefly
static const uint32_t AFTER_REPLY_MS   = 300;

// ---------------- RTC WAKE PIN PULLDOWN ----------------
void setupWakePinPulldown() {
  rtc_gpio_deinit(WAKE_PIN);
  rtc_gpio_set_direction(WAKE_PIN, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pulldown_en(WAKE_PIN);   // keep LOW when floating
  rtc_gpio_pullup_dis(WAKE_PIN);
}

void goToDeepSleep() {
  setupWakePinPulldown();
  esp_sleep_enable_ext0_wakeup(WAKE_PIN, 1); // wake when HIGH

  Serial.println("Going to DEEP SLEEP. Waiting for AIN1 HIGH...");
  Serial.flush();
  delay(50);

  esp_deep_sleep_start();
}

// ---------------- READ ONE LINE FROM RG-15 ----------------


// ---------------- REQUEST RAIN FROM RG-15 ----------------
bool readRainAccumLine(char *out, size_t maxLen) {
  while (Serial1.available()) Serial1.read();  // flush old bytes
  Serial1.print("A\r\n");
  return readRG15Line(out, maxLen, 1500);
}

// ---------------- SEND RESPONSE TO RADIO ----------------
void sendToRadio(const char *payloadLine) {
  // Reply format: RF1:<rg15 line>
  Serial2.print(MY_SENSOR_ID);
  Serial2.print(":");
  Serial2.print(payloadLine);
  Serial2.print("\n");
  Serial2.flush();
}

// ---------------- HANDLE ONE RADIO LINE ----------------
bool handleRadioLine(char *line) {
  // Expected incoming format: WM1:RF1:RAIN?
  // We ignore "WM1", match "RF1", execute "RAIN?"
  Serial.print("UART RAW: [");
  Serial.print(line);
  Serial.println("]");

  // Find first colon (after WM1)
  char *firstColon = strchr(line, ':');
  if (!firstColon) return false;

  char *afterSender = firstColon + 1;
  while (*afterSender == ' ') afterSender++;

  // Find second colon (between RF1 and command)
  char *secondColon = strchr(afterSender, ':');
  if (!secondColon) return false;

  *secondColon = '\0';
  char *targetId = afterSender;

  char *cmd = secondColon + 1;
  while (*cmd == ' ') cmd++;

  if (strcmp(targetId, MY_SENSOR_ID) != 0) {
    Serial.print("Ignored (target mismatch): ");
    Serial.println(targetId);
    return false;
  }

  Serial.print("Accepted cmd for ");
  Serial.print(targetId);
  Serial.print(": ");
  Serial.println(cmd);

  if (strncmp(cmd, "RAIN?", 5) == 0) {
    if (readRainAccumLine(rgLine, sizeof(rgLine))) {
      sendToRadio(rgLine);     // e.g. "Acc  0.00 mm"
    } else {
      sendToRadio("ERR");
    }
    return true;
  } else {
    sendToRadio("ERROR: UNKNOWN CMD");
    return true;
  }
}

// ---------------- READ UART STREAM ----------------
bool processRadioStream() {
  // returns true if we handled a command for this node
  while (Serial2.available()) {
    char c = (char)Serial2.read();

    if (c == '\n' || c == '\r') {
      if (radioIdx == 0) continue;

      radioBuf[radioIdx] = '\0';
      radioIdx = 0;

      if (handleRadioLine(radioBuf)) return true;

    } else {
      if (radioIdx < sizeof(radioBuf) - 1) {
        radioBuf[radioIdx++] = c;
      } else {
        radioIdx = 0; // overflow protection
      }
    }
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Power savers while awake
  WiFi.mode(WIFI_OFF);
  btStop();
  esp_bt_controller_disable();
  setCpuFrequencyMhz(80);

  // Start UARTs ASAP (important after wake)
  Serial1.begin(RG_BAUD, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);
  Serial2.begin(RADIO_BAUD, SERIAL_8N1, RADIO_RX_PIN, RADIO_TX_PIN);

  // IMPORTANT: Do NOT flush Serial2 here (could discard early bytes)
  radioIdx = 0;

  Serial.println("\nRF1 deep-sleep on-demand reader (wake via RAK AIN1 HIGH)");
}

void loop() {
  const uint8_t MIN_CMDS = 2;          // <-- handle at least 2 commands
  uint8_t handledCount = 0;

  uint32_t startMs = millis();

  // Keep listening until we handled 2 commands, or time runs out
  while ((millis() - startMs) < ACTIVE_WINDOW_MS && handledCount < MIN_CMDS) {

    // processRadioStream() returns true only when a valid command for RF1 was handled
    if (processRadioStream()) {
      handledCount++;
      Serial.print("Handled commands: ");
      Serial.println(handledCount);

      // Give UART/radio a moment after replying (optional)
      delay(AFTER_REPLY_MS);
    } else {
      // No complete valid line yet
      delay(5);
    }
  }

  if (handledCount < MIN_CMDS) {
    Serial.print("Timeout. Only handled ");
    Serial.print(handledCount);
    Serial.println(" command(s).");
  } else {
    Serial.println("Handled minimum commands. Going to sleep...");
  }

  // Wait for AIN1 to go LOW before sleeping again
  Serial.println("Waiting for AIN1 LOW...");
  setupWakePinPulldown();
  while (rtc_gpio_get_level(WAKE_PIN) == 1) {
    delay(10);
  }
  Serial.println("AIN1 went LOW and go to deep sleep");
  goToDeepSleep();
}


