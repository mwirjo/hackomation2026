#include <Arduino.h>

HardwareSerial ESPSerial(1); 

#define ESP_TX_PIN 33  // Verbonden met Nano Pin 10
#define ESP_RX_PIN 32  // Verbonden met Nano Pin 11 (Luistert nu!)

int pingCounter = 1;

void sendPing();
void forwardCommand(String cmd);
void listenToNano();

void setup() {
  Serial.begin(115200);
  ESPSerial.begin(9600, SERIAL_8N1, ESP_RX_PIN, ESP_TX_PIN);
  
  Serial.println("ESP32 Two-Way Command Center Ready (Pins 32 & 33).");
}

void loop() {
  // 1. Lees invoer van de computer om commando's te versturen
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim(); 

    if (input.length() > 0) {
      if (input.equalsIgnoreCase("ping")) {
        sendPing();
      } else {
        forwardCommand(input);
      }
    }
  }

  // 2. NIEUW: Luister continu of de Nano antwoord geeft
  listenToNano();
}

void sendPing() {
  String pingMessage = "Ping packet #" + String(pingCounter);
  ESPSerial.println(pingMessage);
  Serial.print("[SENT] ");
  Serial.println(pingMessage);
  pingCounter++; 
}

void forwardCommand(String cmd) {
  ESPSerial.println(cmd);
  Serial.print("[SENT] Command: ");
  Serial.println(cmd);
}

// Functie die rapportages van de Nano opvangt
void listenToNano() {
  if (ESPSerial.available() > 0) {
    String report = ESPSerial.readStringUntil('\n');
    report.trim();
    
    if (report.length() > 0) {
      Serial.print("[REPORT FROM NANO] ");
      Serial.println(report);
    }
  }
}
