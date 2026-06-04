#include <AccelStepper.h>

// Motor 1 pins
#define M1_OUTPUT1   5
#define M1_OUTPUT2   4
#define M1_OUTPUT3   3
#define M1_OUTPUT4   2

// Motor 2 pins
#define M2_OUTPUT1   6
#define M2_OUTPUT2   7
#define M2_OUTPUT3   8
#define M2_OUTPUT4   9

const int stepsPerRotation = 2048;

AccelStepper motor1(AccelStepper::FULL4WIRE, M1_OUTPUT1, M1_OUTPUT3, M1_OUTPUT2, M1_OUTPUT4);
AccelStepper motor2(AccelStepper::FULL4WIRE, M2_OUTPUT1, M2_OUTPUT3, M2_OUTPUT2, M2_OUTPUT4);

int sequenceStep = 0;
unsigned long stateMoveTime = 0;
const int stateDelay = 1000; // 1 second delay between moves

void setup() {
  Serial.begin(9600);
  
  motor1.setMaxSpeed(500); 
  motor1.setAcceleration(200);
  
  motor2.setMaxSpeed(500);
  motor2.setAcceleration(200);
}

void loop() {
  // Check if both motors finished their targets and the delay timer expired
  if (!motor1.isRunning() && !motor2.isRunning() && (millis() - stateMoveTime >= stateDelay)) {
    
    // FORCE HARD STOP: Ensures speed profile ramps down to 0 and clears past steps
    motor1.stop();
    motor2.stop();
    motor1.setCurrentPosition(0);
    motor2.setCurrentPosition(0);
    
    if (sequenceStep == 0) {
      Serial.println("M1: 1000 FWD | M2: 1000 FWD");
      motor1.move(1000); 
      motor2.move(1000);
      sequenceStep = 1;
    } 
    else if (sequenceStep == 1) {
      Serial.println("M1: -500 BWD | M2: -500 BWD");
      motor1.move(-500); 
      motor2.move(-500);
      sequenceStep = 2;
    } 
    else if (sequenceStep == 2) {
      Serial.println("M1: FULL FWD | M2: FULL FWD");
      motor1.move(stepsPerRotation);
      motor2.move(stepsPerRotation); 
      sequenceStep = 0; // Reset loop
    }
    
    stateMoveTime = millis(); // Reset delay timer
  }

  // MUST be called constantly to step the motors in the background
  motor1.run();
  motor2.run();
}
