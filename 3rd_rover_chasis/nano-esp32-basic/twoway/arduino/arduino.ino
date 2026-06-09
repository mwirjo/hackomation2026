#include <SoftwareSerial.h>

#define SW_RX 10
#define SW_TX 11  // Deze pin stuurt nu data TERUG naar de ESP32!
SoftwareSerial ESPSerial(SW_RX, SW_TX);

const int ledPin = LED_BUILTIN; 
int ledState = LOW;
unsigned long lastBlinkTime = 0;
const long blinkInterval = 500; 
bool blinkActive = false; 

void handleToggleCommand();

void setup() {
  Serial.begin(9600);
  ESPSerial.begin(9600);
  pinMode(ledPin, OUTPUT);
  Serial.println("Nano Two-Way Ready.");
}

void loop() {
  processComputerInput();

  if (blinkActive) {
    blinkBuiltInLed();
  }

  processEspData();
}

void blinkBuiltInLed() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastBlinkTime >= blinkInterval) {
    lastBlinkTime = currentMillis;
    ledState = !ledState; 
    digitalWrite(ledPin, ledState);
  }
}

void handleToggleCommand() {
  blinkActive = !blinkActive; 
  
  if (blinkActive) {
    Serial.println("Blink activated!");
    ESPSerial.println("ACK: Blink is nu AAN"); // Stuur succes-bericht naar ESP32
  } else {
    Serial.println("Blink stopped!");
    digitalWrite(ledPin, LOW); 
    ESPSerial.println("ACK: Blink is nu UIT"); // Stuur succes-bericht naar ESP32
  }
}

void processComputerInput() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'b' || cmd == 'B') {
      handleToggleCommand();
    }
  }
}

void processEspData() {
  if (ESPSerial.available() > 0) {
    String incoming = ESPSerial.readStringUntil('\n');
    incoming.trim();

    if (incoming.length() > 0) {
      Serial.print("Received from ESP32: ");
      Serial.println(incoming);

      if (incoming == "b" || incoming == "B") {
        handleToggleCommand();
      } else {
        // Voor elk ander onbekend commando sturen we een generieke ACK
        ESPSerial.println("ACK: Commando '" + incoming + "' ontvangen!");
      }
    }
  }
}
