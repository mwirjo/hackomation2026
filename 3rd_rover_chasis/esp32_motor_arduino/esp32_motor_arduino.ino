#include <HardwareSerial.h>

// Use UART2 on ESP32 with your specified pins
HardwareSerial NanoSerial(2);  // UART2

#define ESP_RX_PIN 32   // connects to Nano TX (via level shifter)
#define ESP_TX_PIN 33   // connects to Nano RX (direct)

void setup() {
  Serial.begin(115200);          // USB Serial for debug/monitor
  NanoSerial.begin(9600, SERIAL_8N1, ESP_RX_PIN, ESP_TX_PIN);
  Serial.println("ESP32 ready. Send f/b/l/r/s/o to control Nano.");
}

void loop() {
  // Forward commands from ESP32 USB monitor → Nano
  if (Serial.available()) {
    char cmd = Serial.read();
    NanoSerial.write(cmd);
    Serial.print("Sent to Nano: ");
    Serial.println(cmd);
  }

  // Forward responses from Nano → ESP32 USB monitor
  if (NanoSerial.available()) {
    String msg = NanoSerial.readStringUntil('\n');
    Serial.print("Nano says: ");
    Serial.println(msg);
  }
}