#define CUSTOM_SETTINGS
#define INCLUDE_GAMEPAD_MODULE
#include <DabbleESP32.h>

#include <TinyGPS++.h>
#include <MPU9250_asukiaaa.h>
#include <Wire.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>

// -----------------------------
// Rijmotoren (L298N)
int enableRightMotor = 14;
int rightMotorPin1 = 27;
int rightMotorPin2 = 26;
int enableLeftMotor = 32;
int leftMotorPin1 = 25;
int leftMotorPin2 = 33;

#define MAX_MOTOR_SPEED 255
const int PWMFreq = 1000;
const int PWMResolution = 8;
const int rightMotorPWMSpeedChannel = 4;
const int leftMotorPWMSpeedChannel = 5;

// -----------------------------
// MaaiMotor (BTS7960)
const int R_EN = 12;
const int L_EN = 13;
const int RPWM = 15;
const int LPWM = 0;
const int pwmKanaalRPWM = 0;
const int pwmKanaalLPWM = 1;
const int pwmResolutie = 8;
const int pwmFrequentie = 1000;
const int maaiSnelheid = 40;
bool motorAan = false;
bool vorigeKnopStatus = false;

// -----------------------------
// GPS en IMU
TinyGPSPlus gps;
HardwareSerial neogps(1);
MPU9250_asukiaaa imu;

float yaw = 0.0;
bool mappingActief = false;
unsigned long laatsteLogtijd = 0;
File mapBestand;

// -----------------------------
// Dead Reckoning
float posX = 0.0, posY = 0.0;
unsigned long vorigeTijd = 0;
const float snelheidMax = 0.5;

// -----------------------------
// WiFi Webserver
const char* ssid = "AutoMaaier";
const char* password = "12345678";
WiFiServer server(80);

void setup() {
  Serial.begin(115200);
  Dabble.begin("AutoMaaier");

  pinMode(enableLeftMotor, OUTPUT);
  pinMode(enableRightMotor, OUTPUT);
  pinMode(leftMotorPin1, OUTPUT);
  pinMode(leftMotorPin2, OUTPUT);
  pinMode(rightMotorPin1, OUTPUT);
  pinMode(rightMotorPin2, OUTPUT);

  pinMode(R_EN, OUTPUT);
  pinMode(L_EN, OUTPUT);
  pinMode(RPWM, OUTPUT);
  pinMode(LPWM, OUTPUT);
  ledcSetup(pwmKanaalRPWM, pwmFrequentie, pwmResolutie);
  ledcSetup(pwmKanaalLPWM, pwmFrequentie, pwmResolutie);
  ledcAttachPin(RPWM, pwmKanaalRPWM);
  ledcAttachPin(LPWM, pwmKanaalLPWM);

  ledcSetup(leftMotorPWMSpeedChannel, PWMFreq, PWMResolution);
  ledcAttachPin(enableLeftMotor, leftMotorPWMSpeedChannel);
  ledcSetup(rightMotorPWMSpeedChannel, PWMFreq, PWMResolution);
  ledcAttachPin(enableRightMotor, rightMotorPWMSpeedChannel);

  neogps.begin(9600, SERIAL_8N1, 2, 4);
  Wire.begin();
  imu.setWire(&Wire);
  imu.beginAccel();
  imu.beginGyro();
  imu.beginMag();

  SPIFFS.begin(true);

  WiFi.softAP(ssid, password);
  server.begin();
  Serial.println("WiFi gestart: AutoMaaier");
  Serial.println(WiFi.softAPIP());

  vorigeTijd = millis();
}

void loop() {
  Dabble.processInput();
  joystickBesturing();
  gpsInlezen();
  imuInlezen();
  checkMappingToggle();
  if (mappingActief) {
    updateDeadReckoning();
    logPositie();
  }
  handleClient();
}

void joystickBesturing() {
  int yAxis = GamePad.getYaxisData();
  int xAxis = GamePad.getXaxisData();
  int leftSpeed = yAxis * 35 + xAxis * 35;
  int rightSpeed = yAxis * 35 - xAxis * 35;
  rotateMotor(rightSpeed, leftSpeed);
}

void rotateMotor(int rightMotorSpeed, int leftMotorSpeed) {
  rightMotorSpeed = constrain(rightMotorSpeed, -255, 255);
  leftMotorSpeed = constrain(leftMotorSpeed, -255, 255);
  ledcWrite(leftMotorPWMSpeedChannel, abs(leftMotorSpeed));
  digitalWrite(leftMotorPin1, leftMotorSpeed >= 0);
  digitalWrite(leftMotorPin2, leftMotorSpeed < 0);
  ledcWrite(rightMotorPWMSpeedChannel, abs(rightMotorSpeed));
  digitalWrite(rightMotorPin1, rightMotorSpeed >= 0);
  digitalWrite(rightMotorPin2, rightMotorSpeed < 0);
}

void gpsInlezen() {
  while (neogps.available()) gps.encode(neogps.read());
}

void imuInlezen() {
  imu.accelUpdate();
  imu.magUpdate();
  float magX = imu.magX();
  float magY = imu.magY();
  yaw = atan2(magY, magX) * 180.0 / PI;
  if (yaw < 0) yaw += 360;
}

void checkMappingToggle() {
  bool knop = GamePad.isSquarePressed();
  if (knop && !vorigeKnopStatus) {
    mappingActief = !mappingActief;
    if (mappingActief) {
      mapBestand = SPIFFS.open("/map.txt", FILE_WRITE);
      if (mapBestand) mapBestand.println("X,Y,YAW,LAT,LON");
      vorigeTijd = millis();
    } else if (mapBestand) {
      mapBestand.close();
    }
  }
  vorigeKnopStatus = knop;
}

void updateDeadReckoning() {
  unsigned long nu = millis();
  float dt = (nu - vorigeTijd) / 1000.0;
  vorigeTijd = nu;
  int yAxis = GamePad.getYaxisData();
  float snelheid = (yAxis / 7.0) * snelheidMax;
  float afstand = snelheid * dt;
  float yawRad = yaw * PI / 180.0;
  posX += afstand * cos(yawRad);
  posY += afstand * sin(yawRad);
}

void logPositie() {
  if (millis() - laatsteLogtijd > 1000) {
    if (mapBestand) {
      mapBestand.print(posX, 2); mapBestand.print(",");
      mapBestand.print(posY, 2); mapBestand.print(",");
      mapBestand.print(yaw, 1); mapBestand.print(",");
      mapBestand.print(gps.location.lat(), 6); mapBestand.print(",");
      mapBestand.println(gps.location.lng(), 6);
    }
    laatsteLogtijd = millis();
  }
}

void handleClient() {
  WiFiClient client = server.available();
  if (!client) return;
  while (!client.available()) delay(1);
  String req = client.readStringUntil('\r');
  client.readStringUntil('\n');

  if (req.indexOf("/map.txt") >= 0) {
    File f = SPIFFS.open("/map.txt", "r");
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type: text/plain\n");
    if (f) {
      while (f.available()) client.write(f.read());
      f.close();
    } else {
      client.println("Kan map.txt niet openen");
    }
  } else {
    File html = SPIFFS.open("/index.html", "r");
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type: text/html\n");
    if (html) {
      while (html.available()) client.write(html.read());
      html.close();
    } else {
      client.println("Geen index.html gevonden");
    }
  }
  delay(1);
  client.stop();
}
