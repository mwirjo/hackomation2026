#include <SoftwareSerial.h>

#define SW_RX 10
#define SW_TX 11
SoftwareSerial ESPSerial(SW_RX, SW_TX);

const int ledPin = LED_BUILTIN; 
int ledState = LOW;

unsigned long lastBlinkTime = 0;
const long blinkInterval = 500; 

bool blinkActive = false; 

// --- Declaraties van functies ---
void handleToggleCommand();

void setup() {
  Serial.begin(9600);
  ESPSerial.begin(9600);
  pinMode(ledPin, OUTPUT);
  
  Serial.println("Nano ready. Type 'B' via Computer or ESP32 to start/stop blinking.");
}

void loop() {
  // 1. Check commands from computer (USB)
  processComputerInput();

  // 2. Run blink logic if active
  if (blinkActive) {
    blinkBuiltInLed();
  }

  // 3. Check commands from ESP32
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

// Deze centrale functie regelt nu het daadwerkelijke aan/uit zetten
void handleToggleCommand() {
  blinkActive = !blinkActive; 
  
  if (blinkActive) {
    Serial.println("Blink activated!");
  } else {
    Serial.println("Blink stopped!");
    digitalWrite(ledPin, LOW); 
  }
}

void processComputerInput() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 'b' || cmd == 'B') {
      handleToggleCommand(); // Roep de centrale schakelaar aan
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

      // NIEUW: Als de ESP32 een 'b' of 'B' stuurt, schakel dan ook de LED!
      if (incoming == "b" || incoming == "B") {
        handleToggleCommand(); // Roep de centrale schakelaar aan
      }
    }
  }
}
