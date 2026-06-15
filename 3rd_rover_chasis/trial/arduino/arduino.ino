// ============================================================
//  E.V.E Nano Motor Driver
//  Receives commands from ESP32 via SoftwareSerial
//  Commands:
//    F / B / L / R / S / O  — movement (single char + newline)
//    V<0-100>               — set speed percentage
// ============================================================

#include <AccelStepper.h>
#include <SoftwareSerial.h>

#define SW_RX  10   // Nano RX ← ESP32 TX (via voltage divider)
#define SW_TX  11   // Nano TX → ESP32 RX (direct, 5V tolerant on ESP32 pin 32)

SoftwareSerial ESPSerial(SW_RX, SW_TX);

// Motor wiring
#define M1_OUT1  2
#define M1_OUT2  3
#define M1_OUT3  4
#define M1_OUT4  5
#define M2_OUT1  9
#define M2_OUT2  8
#define M2_OUT3  7
#define M2_OUT4  6

const int STEPS_PER_ROT = 2048;

AccelStepper motor1(AccelStepper::FULL4WIRE, M1_OUT1, M1_OUT3, M1_OUT2, M1_OUT4);
AccelStepper motor2(AccelStepper::FULL4WIRE, M2_OUT1, M2_OUT3, M2_OUT2, M2_OUT4);

// Speed mapping
const int BASE_MAX_SPEED = 500;      // steps/s at 100%
const int BASE_STEPS     = 1000;     // steps per command at 100%
float     speedFactor    = 0.60f;    // matches dashboard default 60%

int currentMaxSpeed = 300;
int currentSteps    = 600;

int  sequenceStep       = 0;
unsigned long stateMoveTime = 0;
const unsigned long STATE_DELAY = 1000;
bool autoLoopActive = false;

/* ─── helpers ─── */
void applySpeed() {
  currentMaxSpeed = (int)(BASE_MAX_SPEED * speedFactor);
  currentSteps    = (int)(BASE_STEPS    * speedFactor);
  if (currentMaxSpeed < 50)  currentMaxSpeed = 50;
  if (currentSteps    < 100) currentSteps    = 100;

  motor1.setMaxSpeed(currentMaxSpeed);
  motor1.setAcceleration(currentMaxSpeed * 0.5f);
  motor2.setMaxSpeed(currentMaxSpeed);
  motor2.setAcceleration(currentMaxSpeed * 0.5f);
}

void forceStop() {
  motor1.stop();
  motor2.stop();
  motor1.setCurrentPosition(0);
  motor2.setCurrentPosition(0);
  autoLoopActive = false;
}

void ack(const char* msg) {
  ESPSerial.println(msg);
  Serial.println(msg);
}

/* ─── command handler ─── */
void handleCommand(char cmd) {
  if (cmd == '\n' || cmd == '\r') return;

  forceStop();

  switch (cmd) {
    case 'F': case 'f':
      motor1.move( currentSteps);
      motor2.move( currentSteps);
      ack("ACK: Moving Forward");
      break;

    case 'B': case 'b':
      motor1.move(-currentSteps);
      motor2.move(-currentSteps);
      ack("ACK: Moving Backward");
      break;

    case 'L': case 'l':
      motor1.move(-currentSteps / 2);
      motor2.move( currentSteps / 2);
      ack("ACK: Turning Left");
      break;

    case 'R': case 'r':
      motor1.move( currentSteps / 2);
      motor2.move(-currentSteps / 2);
      ack("ACK: Turning Right");
      break;

    case 'S': case 's':
      ack("ACK: Stopped");
      break;

    case 'O': case 'o':
      autoLoopActive = true;
      sequenceStep   = 0;
      stateMoveTime  = millis();
      ack("ACK: Auto Mode Active");
      break;

    default:
      Serial.print("Unknown cmd: ");
      Serial.println(cmd);
      break;
  }
}

/* ─── automatic sequence ─── */
void runAutoSequence() {
  if (motor1.isRunning() || motor2.isRunning()) return;
  if (millis() - stateMoveTime < STATE_DELAY)   return;

  forceStop();

  if (sequenceStep == 0) {
    motor1.move( currentSteps);
    motor2.move( currentSteps);
    ack("AUTO: Step 0 FWD");
    sequenceStep = 1;
  } else if (sequenceStep == 1) {
    motor1.move(-currentSteps / 2);
    motor2.move(-currentSteps / 2);
    ack("AUTO: Step 1 BWD");
    sequenceStep = 2;
  } else {
    motor1.move(STEPS_PER_ROT);
    motor2.move(STEPS_PER_ROT);
    ack("AUTO: Step 2 Full Rotation");
    sequenceStep = 0;
  }
  stateMoveTime = millis();
}

/* ─── read from ESP32 ─── */
void readESP() {
  if (!ESPSerial.available()) return;

  String line = ESPSerial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  Serial.print("[ESP32] ");
  Serial.println(line);

  // Speed command: V<0-100>
  if (line.charAt(0) == 'V') {
    int pct = line.substring(1).toInt();
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    speedFactor = pct / 100.0f;
    applySpeed();
    String msg = "ACK: Speed " + String(pct) + "%";
    ack(msg.c_str());
    return;
  }

  // Single-char movement / mode command
  handleCommand(line.charAt(0));
}

/* ─── read from USB Serial (debug) ─── */
void readSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  if (line == "?" || line.equalsIgnoreCase("help")) {
    Serial.println("\n== NANO COMMANDS ==");
    Serial.println(" F/B/L/R/S/O  — movement");
    Serial.println(" V<0-100>     — set speed %");
    Serial.println("===================");
    return;
  }
  if (line.charAt(0) == 'V') { readESP(); return; } // reuse same path
  handleCommand(line.charAt(0));
}

/* ─── setup ─── */
void setup() {
  Serial.begin(9600);
  ESPSerial.begin(9600);

  applySpeed();

  Serial.println("[NANO] Motor driver ready");
  Serial.println("[NANO] Waiting for commands from ESP32...");
}

/* ─── loop ─── */
void loop() {
  readSerial();
  readESP();

  if (autoLoopActive) runAutoSequence();

  motor1.run();
  motor2.run();
}
