#include <AccelStepper.h>
#include <SoftwareSerial.h>

#define SW_RX 10
#define SW_TX 11
SoftwareSerial ESPSerial(SW_RX, SW_TX);

#define M1_OUTPUT1   2
#define M1_OUTPUT2   3
#define M1_OUTPUT3   4
#define M1_OUTPUT4   5
#define M2_OUTPUT1   9
#define M2_OUTPUT2   8
#define M2_OUTPUT3   7
#define M2_OUTPUT4   6

const int stepsPerRotation = 2048;
AccelStepper motor1(AccelStepper::FULL4WIRE, M1_OUTPUT1, M1_OUTPUT3, M1_OUTPUT2, M1_OUTPUT4);
AccelStepper motor2(AccelStepper::FULL4WIRE, M2_OUTPUT1, M2_OUTPUT3, M2_OUTPUT2, M2_OUTPUT4);

int sequenceStep = 0;
unsigned long stateMoveTime = 0;
const int stateDelay = 1000;
bool automaticLoopActive = false;

// --- Functie Declaraties ---
void processComputerInput();
void processEspData();
void runAutomaticSequence();
void handleCommand(char cmd);
void forceResetMotors();
void printNanoHelpMenu(); // De nieuwe help-functie voor de Nano

void setup() {
  Serial.begin(9600);
  ESPSerial.begin(9600);
  
  motor1.setMaxSpeed(500);
  motor1.setAcceleration(200);
  motor2.setMaxSpeed(500);
  motor2.setAcceleration(200);
  
  // Toon direct het menu bij het opstarten van de Nano
  printNanoHelpMenu();
}

void loop() {
  processComputerInput();
  processEspData();

  if (automaticLoopActive) {
    runAutomaticSequence();
  }

  motor1.run();
  motor2.run();
}

// --- Core Functies ---

void printNanoHelpMenu() {
  Serial.println("\n==================================================");
  Serial.println("            NANO ROVER DRIVER CLI MENU            ");
  Serial.println("==================================================");
  Serial.println(" [?] or [help]  : Show this local driver menu");
  Serial.println("");
  Serial.println(" Direct Hardware Commands:");
  Serial.println(" [F] / [f]      : Step both motors Forward (1000 steps)");
  Serial.println(" [B] / [b]      : Step both motors Backward (-1000 steps)");
  Serial.println(" [L] / [l]      : Turn Left (Motor 1 BWD, Motor 2 FWD)");
  Serial.println(" [R] / [r]      : Turn Right (Motor 1 FWD, Motor 2 BWD)");
  Serial.println(" [S] / [s]      : Emergency Hard Stop & Clear Positions");
  Serial.println(" [O] / [o]      : Start Automated Route Sequence Loop");
  Serial.println("==================================================\n");
}

void processComputerInput() {
  if (Serial.available() > 0) {
    String incoming = Serial.readStringUntil('\n');
    incoming.trim();
    
    if (incoming.length() > 0) {
      // Controleer of de computer lokaal om het helpmenu vraagt
      if (incoming == "?" || incoming.equalsIgnoreCase("help")) {
        printNanoHelpMenu();
      } else {
        handleCommand(incoming.charAt(0));
      }
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
      handleCommand(incoming.charAt(0));
    }
  }
}

void handleCommand(char cmd) {
  if (cmd == '\n' || cmd == '\r') return;
  
  forceResetMotors();
  automaticLoopActive = false;

  if (cmd == 'f' || cmd == 'F') {
    Serial.println("Action: Both Forward");
    ESPSerial.println("ACK: Moving Forward");
    motor1.move(1000);
    motor2.move(1000);
  }
  else if (cmd == 'b' || cmd == 'B') {
    Serial.println("Action: Both Backward");
    ESPSerial.println("ACK: Moving Backward");
    motor1.move(-1000);
    motor2.move(-1000);
  }
  else if (cmd == 'l' || cmd == 'L') {
    Serial.println("Action: Turning Left");
    ESPSerial.println("ACK: Turning Left");
    motor1.move(-1000);
    motor2.move(1000);
  }
  else if (cmd == 'r' || cmd == 'R') {
    Serial.println("Action: Turning Right");
    ESPSerial.println("ACK: Turning Right");
    motor1.move(1000);
    motor2.move(-1000);
  }
  else if (cmd == 's' || cmd == 'S') {
    Serial.println("Action: Hard Stop");
    ESPSerial.println("ACK: Stopped");
  }
  else if (cmd == 'o' || cmd == 'O') {
    Serial.println("Mode: Starting Automatic Loop");
    ESPSerial.println("ACK: Auto Mode Active");
    automaticLoopActive = true;
    sequenceStep = 0;
    stateMoveTime = millis();
  }
}

void runAutomaticSequence() {
  if (!motor1.isRunning() && !motor2.isRunning() && (millis() - stateMoveTime >= stateDelay)) {
    forceResetMotors();
    
    if (sequenceStep == 0) {
      Serial.println("Loop Step 0 -> Both FWD 1000");
      ESPSerial.println("AUTO: Step 0 (FWD)");
      motor1.move(1000);
      motor2.move(1000);
      sequenceStep = 1;
    }
    else if (sequenceStep == 1) {
      Serial.println("Loop Step 1 -> Both BWD 500");
      ESPSerial.println("AUTO: Step 1 (BWD)");
      motor1.move(-500);
      motor2.move(-500);
      sequenceStep = 2;
    }
    else if (sequenceStep == 2) {
      Serial.println("Loop Step 2 -> Full rotation FWD");
      ESPSerial.println("AUTO: Step 2 (Full Rotation)");
      motor1.move(stepsPerRotation);
      motor2.move(stepsPerRotation);
      sequenceStep = 0;
    }
    stateMoveTime = millis();
  }
}

void forceResetMotors() {
  motor1.stop();
  motor2.stop();
  motor1.setCurrentPosition(0);
  motor2.setCurrentPosition(0);
}
