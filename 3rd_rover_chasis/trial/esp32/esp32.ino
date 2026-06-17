#include <WiFi.h>
#include <esp_wifi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <AsyncWebSocket.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <SPIFFS.h>

/* =================== PIN CONFIG =================== */
#define NANO_TX_PIN   33
#define NANO_RX_PIN   32

static const uint8_t SDA_PIN   = 21;
static const uint8_t SCL_PIN   = 22;
static const uint8_t BNO_ADDR  = 0x4A;

static const uint8_t GPS_RX    = 16;
static const uint8_t GPS_TX    = 17;

/* =================== DYNAMIC NETWORK PROFILES =================== */
String stSsid           = "IoTLabSu";
String stPass           = "H@ckTe@m)(";

const char* AP_SSID     = "EVE_New_Bot";
const char* AP_PASS     = "arduino987";
const uint8_t WIFI_CH   = 1; 

/* =================== OBJECTS =================== */
AsyncWebServer  server(80);
AsyncWebSocket  ws("/ws");

HardwareSerial  nanoSerial(2);
HardwareSerial  gpsSerial(1);

Adafruit_BNO08x bno;
TinyGPSPlus     gps;

/* =================== STATE =================== */
bool    imuReady     = false;
float   yaw          = 0;
float   heading      = 0;
float   gpsLat       = 0;
float   gpsLng       = 0;
float   currentSpeed = 60.0;
String  currentMode  = "IDLE"; 

unsigned long lastTelemetry = 0;
const unsigned long TELEM_INTERVAL = 100;

String hardwareSerialBuffer = "";
String nanoSerialBuffer     = "";

/* =================== FUNCTION PROTOTYPES =================== */
void handleCLI(String cmd);
void printHelp();
void startAccessPoint();
bool startStationMode();
void loadWiFiCredentials();
void saveWiFiCredentials(String ssid, String pass);
void sendToNano(char cmd);
void readFromNano();
void readHardwareSerial();
void runTelemetryScheduler();
void updateIMU();
void updateGPS();

/* =================== NANO UART =================== */
void sendToNano(char cmd) {
  nanoSerial.print(cmd);
  nanoSerial.print('\n');
  Serial.printf("[NANO TX] '%c'\n", cmd);
}

void readFromNano() {
  while (nanoSerial.available() > 0) {
    char c = nanoSerial.read();
    if (c == '\n') {
      nanoSerialBuffer.trim();
      if (nanoSerialBuffer.length() > 0) {
        bool isMotorEcho = false;
        if (nanoSerialBuffer.length() == 1) {
          char check = nanoSerialBuffer.charAt(0);
          if (check == 'F' || check == 'B' || check == 'L' || check == 'R' || check == 'S' || check == 'O') {
            isMotorEcho = true;
          }
        }
        if (!isMotorEcho) {
          StaticJsonDocument<128> ack;
          ack["log"] = nanoSerialBuffer;
          String out;
          serializeJson(ack, out);
          ws.textAll(out);
        }
      }
      nanoSerialBuffer = ""; 
    } else if (c != '\r') {
      nanoSerialBuffer += c; 
    }
  }
}

/* =================== WEBSOCKET =================== */
void onWS(AsyncWebSocket* srv, AsyncWebSocketClient* client,
          AwsEventType type, void* arg, uint8_t* data, size_t len) {

  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] UI Terminal linked successfully. Mode: %s\n", currentMode.c_str());
    return;
  }
  if (type == WS_EVT_DISCONNECT) {
    sendToNano('S');
    return;
  }
  if (type != WS_EVT_DATA) return;

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, data, len) != DeserializationError::Ok) return;

  if (doc.containsKey("dir")) {
    const char* d = doc["dir"];
    if      (strcmp(d, "F") == 0) sendToNano('F');
    else if (strcmp(d, "B") == 0) sendToNano('B');
    else if (strcmp(d, "L") == 0) sendToNano('L');
    else if (strcmp(d, "R") == 0) sendToNano('R');
    else                          sendToNano('S');
    return;
  }

  if (doc.containsKey("spd")) {
    currentSpeed = doc["spd"];
    String spd = "V" + String((int)currentSpeed);
    nanoSerial.println(spd);
    return;
  }

  if (doc.containsKey("cli")) {
    handleCLI(String((const char*)doc["cli"]));
    return;
  }
}

/* =================== STORAGE DRIVERS =================== */
void loadWiFiCredentials() {
  if (!SPIFFS.exists("/wifi.json")) {
    Serial.println("[STORAGE] No saved Wi-Fi configuration found. Using default fallbacks.");
    return;
  }

  File file = SPIFFS.open("/wifi.json", "r");
  if (!file) {
    Serial.println("[STORAGE] Failed to open Wi-Fi config file.");
    return;
  }

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (!error) {
    stSsid = doc["ssid"].as<String>();
    stPass = doc["pass"].as<String>();
    Serial.println("[STORAGE] Loaded custom Wi-Fi credentials from SPIFFS.");
  } else {
    Serial.println("[STORAGE] Failed to parse Wi-Fi config JSON.");
  }
}

void saveWiFiCredentials(String ssid, String pass) {
  File file = SPIFFS.open("/wifi.json", "w");
  if (!file) {
    Serial.println("[STORAGE] Critical error: Could not create Wi-Fi config file!");
    return;
  }

  StaticJsonDocument<256> doc;
  doc["ssid"] = ssid;
  doc["pass"] = pass;

  if (serializeJson(doc, file) == 0) {
    Serial.println("[STORAGE] Failed to write data to file.");
  } else {
    Serial.println("[STORAGE] New Wi-Fi credentials successfully saved to flash memory.");
  }
  file.close();
}

/* =================== EXCLUSIVE RADIO DRIVERS =================== */
void startAccessPoint() {
  Serial.println("[WiFi] Turning off Station stack...");
  WiFi.disconnect(true, true); 
  
  Serial.println("[WiFi] Activating Hardcoded Access Point...");
  WiFi.mode(WIFI_AP);
  
  esp_wifi_set_channel(WIFI_CH, WIFI_SECOND_CHAN_NONE);
  WiFi.softAP(AP_SSID, AP_PASS, WIFI_CH);
  
  currentMode = "AP Mode";
  Serial.printf("[WiFi] Active Broadcast Name: %s\n", AP_SSID);
  Serial.printf("[WiFi] Direct Connection Portal IP: %s\n", WiFi.softAPIP().toString().c_str());
}

bool startStationMode() {
  if (stSsid.length() == 0) {
    Serial.println("[WiFi] No home network configuration found. Skipping Station step.");
    return false;
  }

  Serial.printf("[WiFi] Attempting connection to: %s\n", stSsid.c_str());
  WiFi.softAPdisconnect(true); 
  WiFi.mode(WIFI_STA);
  
  WiFi.begin(stSsid.c_str(), stPass.c_str());

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected to Home Network! Local Station IP: %s\n", WiFi.localIP().toString().c_str());
    currentMode = "STA Mode";
    return true;
  } else {
    Serial.println("\n[WiFi] Connection timed out. Router not responding.");
    return false;
  }
}

/* =================== CORE CLI PARSER =================== */
/* =================== CORE CLI PARSER =================== */
void handleCLI(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.printf("[CLI EXEC] %s\n", cmd.c_str());

  if (cmd == "help" || cmd == "?") {
    printHelp();
  }
  else if (cmd == "imu") {
    Serial.println("\n--- IMU DIAGNOSTIC REPORT ---");
    if (imuReady) {
      Serial.printf("Status:  ONLINE\n");
      Serial.printf("Yaw:     %.2f°\n", yaw);
      Serial.printf("Heading: %.2f°\n", heading);
    } else {
      Serial.println("Status:  OFFLINE (Check BNO08x I2C Wiring at 0x4A)");
    }
    Serial.println("-----------------------------\n");
  }
  else if (cmd == "gps") {
    Serial.println("\n--- GPS DIAGNOSTIC REPORT ---");
    Serial.printf("Fix Status: %s\n", gps.location.isValid() ? "3D FIX ACQUIRED" : "NO FIX (Searching...)");
    Serial.printf("Satellites: %d\n", gps.satellites.value());
    Serial.printf("Latitude:   %.6f\n", gpsLat);
    Serial.printf("Longitude:  %.6f\n", gpsLng);
    Serial.printf("Altitude:   %.2fm\n", gps.altitude.meters());
    Serial.printf("HDOP:       %.2f\n", gps.hdop.value() / 100.0);
    Serial.println("-----------------------------\n");
  }
  else if (cmd.startsWith("wifi ")) {
    String args = cmd.substring(5);
    int commaIdx = args.indexOf(',');
    
    if (commaIdx != -1) {
      String newSsid = args.substring(0, commaIdx);
      String newPass = args.substring(commaIdx + 1);
      newSsid.trim();
      newPass.trim();

      if (newSsid.length() > 0) {
        stSsid = newSsid;
        stPass = newPass;
        saveWiFiCredentials(stSsid, stPass);
        
        Serial.println("[WiFi] Re-initializing connection with new credentials...");
        if (!startStationMode()) {
          startAccessPoint();
        }
      }
    } else {
      Serial.println("[CLI ERROR] Invalid format. Use: wifi SSID,PASSWORD");
    }
  }
  else if (cmd.startsWith("mode ")) {
    String mode = cmd.substring(5);
    if (mode == "auto") sendToNano('S');
  }
  else if (cmd.startsWith("auto ")) {
    String action = cmd.substring(5);
    if      (action == "stop")    { sendToNano('S'); }
    else if (action == "scan")    { sendToNano('O'); }
  }
  else {
    Serial.println("[CLI ERROR] Unknown command. Type 'help' or '?' for available options.");
  }
}

void printHelp() {
  Serial.println("\n==================================================");
  Serial.println("            E.V.E CONTROLLER SYSTEM CLI            ");
  Serial.println("==================================================");
  Serial.println("Wi-Fi Configuration Command:");
  Serial.println("  wifi SSID,PASS - Set new credentials & reconnect");
  Serial.println("\nDirect Motor Overrides (Single Characters):");
  Serial.println("  F            - Move Forward");
  Serial.println("  B            - Move Backward");
  Serial.println("  L            - Turn Left");
  Serial.println("  R            - Turn Right");
  Serial.println("  S            - Stop All Motors");
  Serial.println("  O            - Trigger Environment Scan");
  Serial.println("\nSystem & Navigation Commands:");
  Serial.println("  mode auto    - Switch controller state to AUTONOMOUS");
  Serial.println("  auto scan    - Execute autonomous scanner track");
  Serial.println("  auto stop    - Halt active autonomous sequence");
  Serial.println("  help / ?     - Display this command manual");
  Serial.println("  imu          - Show IMU data");
  Serial.println("  gps          - Show GPS data");
  Serial.println("==================================================\n");
}

/* =================== HARDWARE SERIAL PARSER =================== */
void readHardwareSerial() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') {
      hardwareSerialBuffer.trim();
      
      if (hardwareSerialBuffer.length() > 0) {
        if (hardwareSerialBuffer.length() == 1) {
          char cmd = hardwareSerialBuffer.charAt(0);
          if (cmd == 'F' || cmd == 'B' || cmd == 'L' || cmd == 'R' || cmd == 'S' || cmd == 'O') {
            sendToNano(cmd);
          } else {
            handleCLI(hardwareSerialBuffer);
          }
        } else {
          handleCLI(hardwareSerialBuffer);
        }
        hardwareSerialBuffer = ""; 
      }
    } else {
      hardwareSerialBuffer += c;
    }
  }
}

/* =================== TELEMETRY SCHEDULER =================== */
void runTelemetryScheduler() {
  unsigned long now = millis();
  if (now - lastTelemetry >= TELEM_INTERVAL) {
    lastTelemetry = now;

    StaticJsonDocument<384> doc;
    doc["yaw"]         = yaw;
    doc["heading"]     = heading;
    doc["lat"]         = gpsLat;
    doc["lng"]         = gpsLng;
    doc["gpsOk"]       = gps.location.isValid();
    doc["sats"]        = gps.satellites.value();
    doc["speed"]       = currentSpeed;
    doc["wifiStatus"]  = currentMode;
    doc["staIp"]       = (WiFi.getMode() == WIFI_STA) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();

    String out;
    serializeJson(doc, out);
    ws.textAll(out);
  }
  ws.cleanupClients();
}

/* =================== IMU & GPS TARGETS =================== */
void updateIMU() {
  if (!imuReady) return;
  sh2_SensorValue_t sv;
  if (!bno.getSensorEvent(&sv)) return;
  if (sv.sensorId != SH2_ROTATION_VECTOR) return;
  float qi = sv.un.rotationVector.i;
  float qj = sv.un.rotationVector.j;
  float qk = sv.un.rotationVector.k;
  float qr = sv.un.rotationVector.real;
  yaw     = atan2f(2*(qr*qk + qi*qj), 1 - 2*(qj*qj + qk*qk)) * 180.0f / PI;
  heading = yaw;
}

void updateGPS() {
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }
  if (gps.location.isValid()) {
    gpsLat = gps.location.lat();
    gpsLng = gps.location.lng();
  }
}

/* =================== SETUP =================== */
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n[SETUP] E.V.E Controller Starting (Fixed Boot Sequence)...");

  Wire.begin(SDA_PIN, SCL_PIN);
  SPIFFS.begin(true);

  loadWiFiCredentials();

  nanoSerial.begin(9600, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  imuReady = bno.begin_I2C(BNO_ADDR);
  if (imuReady) bno.enableReport(SH2_ROTATION_VECTOR);

  // STEP 1: INITIALIZE WIFI STACK FIRST (This sets up LwIP and allocates mbox memory)
  if (!startStationMode()) {
    startAccessPoint();
  }

  // STEP 2: SETUP SERVER AND WEBSOCKET ROUTING (Safe to bind now)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(SPIFFS, "/index.html", "text/html");
  });
  server.onNotFound([](AsyncWebServerRequest* req) {
    String path = req->url();
    if (SPIFFS.exists(path)) req->send(SPIFFS, path);
    else req->send(404, "text/plain", "Not found");
  });

  ws.onEvent(onWS);
  server.addHandler(&ws);
  server.begin(); 
  
  printHelp(); 
}

/* =================== CLEAN LOOP =================== */
void loop() {
  updateGPS();
  updateIMU();
  readFromNano();
  readHardwareSerial();
  runTelemetryScheduler();
}
