#include <Arduino.h>

HardwareSerial ESPSerial(2); 

#define ESP_TX_PIN 33
#define ESP_RX_PIN 32 

int pingCounter = 1;

// --- Functie Declaraties ---
void sendPing();
void forwardCommand(String cmd);

void setup() {
  Serial.begin(115200);
  ESPSerial.begin(9600, SERIAL_8N1, ESP_RX_PIN, ESP_TX_PIN);
  
  Serial.println("ESP32 Command Center Ready.");
  Serial.println("-> Type 'ping' to trigger the custom ping function.");
  Serial.println("-> Type any other text to forward it directly to the Nano.");
}

void loop() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim(); 

    if (input.length() > 0) {
      // Controleer of de gebruiker 'ping' heeft getypt (hoofdletterongevoelig)
      if (input.equalsIgnoreCase("ping")) {
        sendPing();
      } else {
        forwardCommand(input);
      }
    }
  }
}

// --- Losse Functies ---

// De aparte ping-functie
void sendPing() {
  String pingMessage = "Ping packet #" + String(pingCounter);
  
  // Stuur naar de Nano
  ESPSerial.println(pingMessage);
  
  // Log naar de eigen computer monitor
  Serial.print("[PING FUNCTION] Sent to Nano: ");
  Serial.println(pingMessage);
  
  pingCounter++; // Hoog de teller op voor de volgende keer
}

// Functie voor alle andere willekeurige commando's
void forwardCommand(String cmd) {
  ESPSerial.println(cmd);
  
  Serial.print("[FORWARD] Sent to Nano: ");
  Serial.println(cmd);
}
