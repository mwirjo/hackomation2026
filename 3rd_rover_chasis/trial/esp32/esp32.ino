// ============================================================
//  E.V.E Robot Controller — INTEGRATED (ROUTED TO /data/)
//  WiFi AP + Native NVS Storage (Dynamic Home Network Pairing)
//  IMU (BNO08x) + GPS telemetry
//  UART bridge → Arduino Nano motor driver
// ============================================================

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
#include <Preferences.h> // Native ESP32 non-volatile storage library

/* =================== PIN CONFIG =================== */
#define NANO_TX_PIN   33   // ESP32 TX → voltage divider → Nano RX
#define NANO_RX_PIN   32   // ESP32 RX ← Nano TX

// IMU (BNO08x) — I2C
static const uint8_t SDA_PIN   = 21;
static const uint8_t SCL_PIN   = 22;
static const uint8_t BNO_ADDR  = 0x4A;

// GPS — UART2
static const uint8_t GPS_RX    = 16;
static const uint8_t GPS_TX    = 17;

/* =================== WiFi AP =================== */
static const char* AP_SSID    = "EVE_New_Bot";
static const char* AP_PASS    = "arduino987";
static const uint8_t WIFI_CH  = 1;

/* =================== OBJECTS =================== */
AsyncWebServer  server(80);
AsyncWebSocket  ws("/ws");

HardwareSerial  nanoSerial(2);   
HardwareSerial  gpsSerial(1);    

Adafruit_BNO08x bno;
TinyGPSPlus     gps;
Preferences     preferences; // Hardware storage handle

/* =================== STATE =================== */
bool    imuReady     = false;
float   yaw          = 0;
float   heading      = 0;
float   gpsLat       = 0;
float   gpsLng       = 0;
float   currentSpeed = 60.0;    // 0–100 %

// Stored Runtime Credentials
String  savedSSID    = "";
String  savedPass    = "";

// Telemetry timer
unsigned long lastTelemetry = 0;
const    unsigned long TELEM_INTERVAL = 100; // ms

void handleCLI(String cmd);

/* =================== NVS FLASH STORAGE MANAGEMENT =================== */
void loadWiFiCredentials() {
  preferences.begin("eve_net", true); // Open storage space in Read-Only mode
  savedSSID = preferences.getString("ssid", "");
  savedPass = preferences.getString("pass", "");
  preferences.end();

  if (savedSSID.length() > 0) {
    Serial.printf("[STORAGE] Found saved home network profile: %s\n", savedSSID.c_str());
  } else {
    Serial.println("[STORAGE] No home network profiles saved yet.");
  }
}

void saveWiFiCredentials(String ssid, String pass) {
  preferences.begin("eve_net", false); // Open storage space in Read-Write mode
  preferences.putString("ssid", ssid);
  preferences.putString("pass", pass);
  preferences.end();

  savedSSID = ssid;
  savedPass = pass;
  Serial.printf("[STORAGE] Successfully saved network profile: %s to flash memory.\n", savedSSID.c_str());
}

/* =================== NANO UART =================== */
void sendToNano(char cmd) {
  nanoSerial.print(cmd);
  nanoSerial.print('\n');
  Serial.printf("[NANO TX] '%c'\n", cmd);
}

void readFromNano() {
  while (nanoSerial.available()) {
    String line = nanoSerial.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) return;
    Serial.printf("[NANO RX] %s\n", line.c_str());

    StaticJsonDocument<128> ack;
    ack["log"] = line;
    String out;
    serializeJson(ack, out);
    ws.textAll(out);
  }
}

/* =================== WEBSOCKET =================== */
void onWS(AsyncWebSocket* srv, AsyncWebSocketClient* client,
          AwsEventType type, void* arg, uint8_t* data, size_t len) {

  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS] Client #%u connected\n", client->id());
    return;
  }
  if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS] Client #%u disconnected\n", client->id());
    sendToNano('S'); 
    return;
  }
  if (type != WS_EVT_DATA) return;

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, data, len) != DeserializationError::Ok) return;

  if (doc.containsKey("dir")) {
    const char* d = doc["dir"];
    Serial.printf("[WS] dir=%s @ %.0f%%\n", d, currentSpeed);

    if      (strcmp(d, "F") == 0) sendToNano('F');
    else if (strcmp(d, "B") == 0) sendToNano('B');
    else if (strcmp(d, "L") == 0) sendToNano('L');
    else if (strcmp(d, "R") == 0) sendToNano('R');
    else                          sendToNano('S');
    return;
  }

  if (doc.containsKey("spd")) {
    currentSpeed = doc["spd"];
    Serial.printf("[WS] speed=%.0f%%\n", currentSpeed);
    String spd = "V" + String((int)currentSpeed);
    nanoSerial.println(spd);
    Serial.printf("[NANO TX] '%s'\n", spd.c_str());
    return;
  }

  if (doc.containsKey("cli")) {
    handleCLI(String((const char*)doc["cli"]));
    return;
  }

  if (doc.containsKey("waypoints")) {
    Serial.println("[PATH] Waypoints received — autonomous path queued");
    return;
  }
}

/* =================== CLI HANDLER =================== */
void handleCLI(String cmd) {
  cmd.trim();
  Serial.printf("[CLI] %s\n", cmd.c_str());

  if (cmd.startsWith("mode ")) {
    String mode = cmd.substring(5);
    Serial.printf("[CLI] Mode → %s\n", mode.c_str());
    if (mode == "auto") {
      sendToNano('S');
    }
  }
  else if (cmd.startsWith("auto ")) {
    String action = cmd.substring(5);
    Serial.printf("[CLI] Auto → %s\n", action.c_str());

    if      (action == "stop")    { sendToNano('S'); }
    else if (action == "runpath") { }
    else if (action == "scan")    { sendToNano('O'); } 
    else if (action == "home")    { sendToNano('S'); } 
  }
  else if (cmd.startsWith("attach ")) {
    String rest   = cmd.substring(7);
    int    space  = rest.indexOf(' ');
    String name   = (space > 0) ? rest.substring(0, space) : rest;
    String state  = (space > 0) ? rest.substring(space + 1) : "off";
    Serial.printf("[CLI] Attachment %s → %s\n", name.c_str(), state.c_str());
  }
  else if (cmd.startsWith("sched ")) {
    Serial.printf("[CLI] Scheduler: %s\n", cmd.c_str());
  }
  // ─── ROBUST WIFI PROVISIONING COMMAND ───
  else if (cmd.startsWith("wifi ")) {
    String args = cmd.substring(5);
    args.trim();
    int space = args.indexOf(' ');
    
    String targetSsid = "";
    String targetPass = "";
    
    if (space > 0) {
      targetSsid = args.substring(0, space);
      targetPass = args.substring(space + 1);
    } else if (args.length() > 0) {
      targetSsid = args; // Handles open networks with zero password strings
      targetPass = "";
    }
    
    if (targetSsid.length() > 0) {
      Serial.printf("[CLI] New credentials received. Connecting to: %s\n", targetSsid.c_str());
      ws.textAll("{\"log\":\"Saving credentials to NVS storage & linking...\"}");
      
      // Save credentials straight into NVS flash memory slots
      saveWiFiCredentials(targetSsid, targetPass);
      
      // Trigger background connection attempt
      if (savedPass.length() > 0) {
        WiFi.begin(savedSSID.c_str(), savedPass.c_str());
      } else {
        WiFi.begin(savedSSID.c_str());
      }
    } else {
      ws.textAll("{\"log\":\"CLI Error: Syntax must be 'wifi <ssid> <password>'\"}");
    }
  }
}

/* =================== IMU =================== */
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

/* =================== GPS =================== */
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
  Serial.println("\n[SETUP] E.V.E Integrated Controller starting…");

  Wire.begin(SDA_PIN, SCL_PIN);

  // SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("[FS] SPIFFS FAILED");
  } else {
    Serial.println("[FS] SPIFFS OK");
    File root = SPIFFS.open("/");
    File f    = root.openNextFile();
    while (f) { Serial.printf("  %s\n", f.name()); f = root.openNextFile(); }
  }

  // Configure Dual Station + Access point settings
  WiFi.mode(WIFI_AP_STA);
  esp_wifi_set_channel(WIFI_CH, WIFI_SECOND_CHAN_NONE);
  
  // Bring local dashboard AP up instantly
  WiFi.softAP(AP_SSID, AP_PASS, WIFI_CH);
  Serial.printf("[WiFi] AP Active: %s | Access IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  // Check storage partitions for home pairing definitions
  loadWiFiCredentials();
  if (savedSSID.length() > 0) {
    Serial.printf("[WiFi] Background home link network attempting connection to: %s\n", savedSSID.c_str());
    if (savedPass.length() > 0) {
      WiFi.begin(savedSSID.c_str(), savedPass.c_str());
    } else {
      WiFi.begin(savedSSID.c_str());
    }
  }

  nanoSerial.begin(9600, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  imuReady = bno.begin_I2C(BNO_ADDR);
  if (imuReady) {
    bno.enableReport(SH2_ROTATION_VECTOR);
    Serial.println("[IMU] BNO08x OK");
  } else {
    Serial.println("[IMU] BNO08x not found");
  }

  server.serveStatic("/", SPIFFS, "/data/").setDefaultFile("index.html");
  
  server.onNotFound([](AsyncWebServerRequest* req) {
    String path = req->url();
    String dataPath = "/data" + path;
    if (SPIFFS.exists(dataPath)) {
      req->send(SPIFFS, dataPath);
    } else {
      req->send(404, "text/plain", "Not found: " + path);
    }
  });

  ws.onEvent(onWS);
  server.addHandler(&ws);

  server.begin();
  Serial.println("[HTTP] Server running on port 80");
}

/* =================== LOOP =================== */
void loop() {
  updateGPS();
  updateIMU();
  readFromNano();

  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.length() > 0) {
      char cmd = input.charAt(0);
      if (cmd == 'F' || cmd == 'B' || cmd == 'L' || cmd == 'R' || cmd == 'S' || cmd == 'O') {
        sendToNano(cmd); 
      } else {
        Serial.printf("[SYSTEM] Unknown Serial command: '%c'\n", cmd);
      }
    }
  }

  unsigned long now = millis();
  if (now - lastTelemetry >= TELEM_INTERVAL) {
    lastTelemetry = now;

    StaticJsonDocument<384> doc;
    doc["yaw"]     = yaw;
    doc["heading"] = heading;
    doc["lat"]     = gpsLat;
    doc["lng"]     = gpsLng;
    doc["gpsOk"]   = gps.location.isValid();
    doc["sats"]    = gps.satellites.value();
    doc["speed"]   = currentSpeed;

    // Telemetry Provisioning Stream Updates
    if (WiFi.status() == WL_CONNECTED) {
      doc["wifiStatus"] = "Connected";
      doc["staIp"]      = WiFi.localIP().toString();
    } else if (savedSSID.length() > 0) {
      doc["wifiStatus"] = "Connecting";
      doc["staIp"]      = "Disconnected";
    } else {
      doc["wifiStatus"] = "Not Configured";
      doc["staIp"]      = "Disconnected";
    }

    String out;
    serializeJson(doc, out);
    ws.textAll(out);
  }

  ws.cleanupClients();
}