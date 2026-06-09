#include <Arduino.h>

HardwareSerial ESPSerial(1); 

#define ESP_TX_PIN 33  // Naar Spanningsteler -> Nano Pin 10 (RX)
#define ESP_RX_PIN 32  // Naar Nano Pin 11 (TX)

int pingCounter = 1;

// --- Functie Declaraties ---
void sendPing();
void forwardCommand(String cmd);
void listenToNano();
void printHelpMenu(); // De nieuwe help-functie

void setup() {
  Serial.begin(115200);
  ESPSerial.begin(9600, SERIAL_8N1, ESP_RX_PIN, ESP_TX_PIN);
  
  // Toon direct het helpmenu bij het opstarten
  printHelpMenu();
}

void loop() {
  // 1. Lees invoer van de computer
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim(); 

    if (input.length() > 0) {
      // Controleer of de gebruiker om hulp vraagt via '?' of 'help'
      if (input == "?" || input.equalsIgnoreCase("help")) {
        printHelpMenu();
      } 
      // Controleer op de ping-functie
      else if (input.equalsIgnoreCase("ping")) {
        sendPing();
      } 
      // Stuur alle andere commando's door naar de Nano rover
      else {
        forwardCommand(input);
      }
    }
  }

  // 2. Luister continu naar feedback van de Nano
  listenToNano();
}

// --- Custom Functies ---

// De ingebouwde CLI help-functie
void printHelpMenu() {
  Serial.println("\n==================================================");
  Serial.println("          ROVER COMMAND CENTER CLI MENU           ");
  Serial.println("==================================================");
  Serial.println(" [?] or [help]  : Show this help menu");
  Serial.println(" [ping]         : Trigger ESP32 status check packet");
  Serial.println("");
  Serial.println(" Rover Movement Commands (Forwarded to Nano):");
  Serial.println(" [F]            : Move Both Motors Forward");
  Serial.println(" [B]            : Move Both Motors Backward");
  Serial.println(" [L]            : Turn Rover Left");
  Serial.println(" [R]            : Turn Rover Right");
  Serial.println(" [S]            : Hard Stop All Motors");
  Serial.println(" [O]            : Start Automatic Sequence Loop");
  Serial.println("==================================================\n");
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
  Serial.print("[SENT TO ROVER] Command: ");
  Serial.println(cmd);
}

void listenToNano() {
  if (ESPSerial.available() > 0) {
    String report = ESPSerial.readStringUntil('\n');
    report.trim();
    
    if (report.length() > 0) {
      Serial.print("[REPORT FROM ROVER] ");
      Serial.println(report);
    }
  }
}
