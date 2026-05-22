#include <Arduino.h>
#include"rg15.h"

// ---------- PINS (KEEP AS YOU REQUESTED) ----------
#define SENSOR_RX_PIN   21   // ESP32 RX  <- RG-15 TX
#define SENSOR_TX_PIN   22   // ESP32 TX  -> RG-15 RX
#define RADIO_RX_PIN    16   // ESP32 RX  <- Radio TX
#define RADIO_TX_PIN    17   // ESP32 TX  -> Radio RX


// ---------- BAUD RATES ----------
#define RG_BAUD     9600
#define RADIO_BAUD  38400

// ---------- SENSOR ID FOR COMMAND ROUTING ----------
const char* MY_SENSOR_ID = "RF1";

// ---------- BUFFERS ----------
char radioBuf[96];
uint8_t radioIdx = 0;

char rgLine[64];

// ---------------- READ ONE LINE FROM RG-15 ----------------


// ---------------- REQUEST RAIN FROM RG-15 ----------------
bool readRainAccumLine(char *out, size_t maxLen) {
  // Flush old bytes so we don't read stale lines
  while (Serial1.available()) Serial1.read();

  // RG-15 command for accumulated rainfall is "A"
  Serial1.print("A\r\n");

  // Read response line (adjust timeout if needed)
  return readRG15Line(out, maxLen, 1500);
}

// ---------------- SEND RESPONSE TO RADIO ----------------
void sendToRadio(const char *payloadLine) {
  Serial2.print(MY_SENSOR_ID);
  Serial2.print(":");
  Serial2.print(payloadLine);
  Serial2.print("\n");
  Serial2.flush();
}

// ---------------- PROCESS ONE FULL RADIO LINE ----------------
// Expected incoming Meshtastic-style line often looks like:
//   "12345678: RF1:RAIN?"
// We parse it like your water-level code: [radioPrefix]: [KEY]:[CMD]
void handleRadioLine(char *line) {
  // Debug raw
  Serial.print("From FloodSerial RAW: [");
  Serial.print(line);
  Serial.println("]");

  // STEP 1: remove prefix up to first colon
  char *firstColon = strchr(line, ':');
  if (!firstColon) return;

  char *afterRadio = firstColon + 1;
  while (*afterRadio == ' ') afterRadio++;

  // STEP 2: split key and cmd by second colon
  char *secondColon = strchr(afterRadio, ':');
  if (!secondColon) return;

  *secondColon = '\0';
  char *key = afterRadio;

  char *cmd = secondColon + 1;
  while (*cmd == ' ') cmd++;

  // STEP 3: check sensor ID
  if (strcmp(key, MY_SENSOR_ID) != 0) {
    Serial.print("Ignored (sensor ID mismatch): ");
    Serial.println(key);
    return;
  }

  Serial.print("Accepted CMD for ");
  Serial.print(key);
  Serial.print(": ");
  Serial.println(cmd);

  // STEP 4: commands
  if (strncmp(cmd, "RAIN?", 5) == 0) {
    if (readRainAccumLine(rgLine, sizeof(rgLine))) {
      // rgLine example: "Acc  0.00 mm"
      sendToRadio(rgLine);
    } else {
      sendToRadio("ERR");
    }
  } else {
    sendToRadio("ERROR: UNKNOWN CMD");
  }
}

// ---------------- READ RADIO STREAM (LIKE YOUR FIRST CODE) ----------------
void processRadioCommand() {
  while (Serial2.available()) {
    char c = (char)Serial2.read();

    if (c == '\n' || c == '\r') {
      if (radioIdx == 0) continue;

      radioBuf[radioIdx] = '\0';
      radioIdx = 0;

      handleRadioLine(radioBuf);

    } else {
      if (radioIdx < sizeof(radioBuf) - 1) {
        radioBuf[radioIdx++] = c;
      } else {
        // overflow protection
        radioIdx = 0;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);

  // RG-15 UART
  Serial1.begin(RG_BAUD, SERIAL_8N1, SENSOR_RX_PIN, SENSOR_TX_PIN);

  // UART to Meshtastic radio
  Serial2.begin(RADIO_BAUD, SERIAL_8N1, RADIO_RX_PIN, RADIO_TX_PIN);

  delay(300);

  Serial.println("RG-15 on-demand reader started");
  Serial.println("Send command over radio: RF1:RAIN?");
}

void loop() {
  // Only act when command comes from radio
  processRadioCommand();
}
