#include <AccelStepper.h>

// Motor 1 pins
#define M1_OUTPUT1   2
#define M1_OUTPUT2   3
#define M1_OUTPUT3   4
#define M1_OUTPUT4   5

// Motor 2 pins
#define M2_OUTPUT1   9
#define M2_OUTPUT2   8
#define M2_OUTPUT3   7
#define M2_OUTPUT4   6

const int stepsPerRotation = 2048;

AccelStepper motor1(AccelStepper::FULL4WIRE, M1_OUTPUT1, M1_OUTPUT3, M1_OUTPUT2, M1_OUTPUT4);
AccelStepper motor2(AccelStepper::FULL4WIRE, M2_OUTPUT1, M2_OUTPUT3, M2_OUTPUT2, M2_OUTPUT4);

// Loop sequence tracking variables
int sequenceStep = 0;
unsigned long stateMoveTime = 0;
const int stateDelay = 1000; 

// Mode tracking flag
bool automaticLoopActive = false;

void setup() {
  Serial.begin(9600);
  
  motor1.setMaxSpeed(500); 
  motor1.setAcceleration(200);
  
  motor2.setMaxSpeed(500);
  motor2.setAcceleration(200);

  Serial.println("Send commands: f, b, l, r, s, or o (for loop)");
}

// Helper function to cleanly reset positions before executing a new move
void forceResetMotors() {
  motor1.stop();
  motor2.stop();
  motor1.setCurrentPosition(0);
  motor2.setCurrentPosition(0);
}

void loop() {
  // Check for incoming serial commands
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    
    // Ignore newline or carriage return characters from terminal inputs
    if (cmd != '\n' && cmd != '\r') {
      forceResetMotors();
      automaticLoopActive = false; // Turn off automated loop unless 'o' is explicitly pressed

      if (cmd == 'f' || cmd == 'F') {
        Serial.println("Manual: Both Forward");
        motor1.move(1000);
        motor2.move(1000);
      }
      else if (cmd == 'b' || cmd == 'B') {
        Serial.println("Manual: Both Backward");
        motor1.move(-1000);
        motor2.move(-1000);
      }
      else if (cmd == 'l' || cmd == 'L') {
        Serial.println("Manual: Turning Left (M1 BWD, M2 FWD)");
        motor1.move(-1000);
        motor2.move(1000);
      }
      else if (cmd == 'r' || cmd == 'R') {
        Serial.println("Manual: Turning Right (M1 FWD, M2 BWD)");
        motor1.move(1000);
        motor2.move(-1000);
      }
      else if (cmd == 's' || cmd == 'S') {
        Serial.println("Manual: Hard Stop");
        // Motors are already stopped by forceResetMotors() above
      }
      else if (cmd == 'o' || cmd == 'O') {
        Serial.println("Mode: Starting Automatic Loop Sequence");
        automaticLoopActive = true;
        sequenceStep = 0;
        stateMoveTime = millis();
      }
    }
  }

  // Run the automated looping routine if active
  if (automaticLoopActive) {
    if (!motor1.isRunning() && !motor2.isRunning() && (millis() - stateMoveTime >= stateDelay)) {
      
      forceResetMotors();
      
      if (sequenceStep == 0) {
        Serial.println("Loop Step 0 -> M1: 1000 FWD | M2: 1000 FWD");
        motor1.move(1000); 
        motor2.move(1000);
        sequenceStep = 1;
      } 
      else if (sequenceStep == 1) {
        Serial.println("Loop Step 1 -> M1: -500 BWD | M2: -500 BWD");
        motor1.move(-500); 
        motor2.move(-500);
        sequenceStep = 2;
      } 
      else if (sequenceStep == 2) {
        Serial.println("Loop Step 2 -> M1: FULL FWD | M2: FULL FWD");
        motor1.move(stepsPerRotation);
        motor2.move(stepsPerRotation); 
        sequenceStep = 0; 
      }
      
      stateMoveTime = millis(); 
    }
  }

  // Crucial: Must be continuously fed to allow stepping background updates
  motor1.run();
  motor2.run();
}
