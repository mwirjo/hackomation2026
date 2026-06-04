// ============================================================
//  E.V.E Robot Controller – with SPIFFS HTML serving (FIXED)
// ============================================================

#include <WiFi.h>
#include <esp_wifi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <AsyncWebSocket.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <SPIFFS.h>

/* ================= CONFIG ================= */
static const uint8_t MOTOR_MAC[] = {0xC8,0xF0,0x9E,0xF1,0x86,0xBC};
static const char* AP_SSID = "EVE_New_Bot";
static const char* AP_PASS = "arduino987";
static const uint8_t WIFI_CHANNEL = 1;

static const uint8_t SDA_PIN = 21;
static const uint8_t SCL_PIN = 22;
static const uint8_t BNO_ADDR = 0x4A;

static const uint8_t GPS_RX = 16;
static const uint8_t GPS_TX = 17;

/* ================= OBJECTS ================= */
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

Adafruit_BNO08x bno;
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);

/* ================= STATE ================= */
bool imuReady = false;
float yaw = 0, heading = 0;
float lat = 0, lng = 0;
float currentSpeed = 60.0;  // Speed percentage (0-100)

/* ================= ESP-NOW ===== MATCH MOTOR STRUCT ===== */
enum Dir { STOP, FWD, BWD, LEFT, RIGHT };

typedef struct {
  Dir   dir;
  int   steps;
  float yaw;
  float lat;
  float lng;
  float speed;
} Command;

void sendMotorCmd(Dir direction, int stepCount) {
  Command cmd = {};
  cmd.dir   = direction;
  cmd.steps = stepCount;
  cmd.yaw   = yaw;
  cmd.lat   = lat;
  cmd.lng   = lng;
  cmd.speed = currentSpeed / 100.0;  // Convert percentage to 0.0-1.0

  Serial.printf("[MOTOR] Sending: dir=%d steps=%d speed=%.2f\n", 
    direction, stepCount, cmd.speed);

  esp_now_send(MOTOR_MAC, (uint8_t*)&cmd, sizeof(cmd));
}

/* ================= WEBSOCKET HANDLER ================= */
void onWS(AsyncWebSocket* server, AsyncWebSocketClient* client,
          AwsEventType type, void* arg, uint8_t* data, size_t len) {

  if (type != WS_EVT_DATA) return;

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, data, len) != DeserializationError::Ok) return;

  // ===== DIRECTION COMMANDS (from joystick or d-pad) =====
  if (doc.containsKey("dir")) {
    const char* d = doc["dir"];
    
    // For joystick: send continuous small steps
    // For d-pad: sends repeatedly via startCmd() in JavaScript
    // Both work with ~500 steps per command
    int stepCount = 500;

    Serial.printf("[WS] Dir command: %s @ speed %.0f%%\n", d, currentSpeed);

    if      (strcmp(d,"F")==0) sendMotorCmd(FWD,   stepCount);
    else if (strcmp(d,"B")==0) sendMotorCmd(BWD,   stepCount);
    else if (strcmp(d,"L")==0) sendMotorCmd(LEFT,  stepCount/2);  // Turn less than linear
    else if (strcmp(d,"R")==0) sendMotorCmd(RIGHT, stepCount/2);
    else if (strcmp(d,"S")==0) sendMotorCmd(STOP,  0);

    return;
  }

  // ===== SPEED UPDATES (from slider) =====
  if (doc.containsKey("spd")) {
    currentSpeed = doc["spd"];
    Serial.printf("[WS] Speed updated: %.0f%%\n", currentSpeed);
    return;
  }

  // ===== CLI COMMANDS =====
  if (doc.containsKey("cli")) {
    handleCLI(String((const char*)doc["cli"]));
    return;
  }

  // ===== WAYPOINTS =====
  if (doc.containsKey("waypoints")) {
    Serial.println("[PATH] Waypoints received");
    return;
  }
}

/* ================= CLI HANDLER ================= */
void handleCLI(String cmd) {
  cmd.trim();
  Serial.print("[CLI] ");
  Serial.println(cmd);

  if (cmd.startsWith("mode ")) {
    String mode = cmd.substring(5);
    Serial.print("Mode changed to: ");
    Serial.println(mode);
  }
  else if (cmd.startsWith("auto ")) {
    String action = cmd.substring(5);
    Serial.print("Auto command: ");
    Serial.println(action);
  }
  else if (cmd.startsWith("attach ")) {
    Serial.println("Attachment toggle");
  }
  else if (cmd.startsWith("sched ")) {
    Serial.println("Scheduler command");
  }
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n[SETUP] Starting E.V.E Sensor Controller...");

  Wire.begin(SDA_PIN, SCL_PIN);

  // ── SPIFFS ──────────────────────────────────────────────
  if (!SPIFFS.begin(true)) {
    Serial.println("[FS] SPIFFS mount FAILED");
  } else {
    Serial.println("[FS] SPIFFS OK");
    File root = SPIFFS.open("/");
    File f = root.openNextFile();
    while (f) {
      Serial.print("  ");
      Serial.println(f.name());
      f = root.openNextFile();
    }
  }

  /* WiFi AP */
  WiFi.mode(WIFI_AP_STA);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  WiFi.softAP(AP_SSID, AP_PASS, WIFI_CHANNEL);

  Serial.print("[WiFi] AP IP: ");
  Serial.println(WiFi.softAPIP());

  /* ESP-NOW */
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] FAILED");
    while(true);
  }
  Serial.println("[ESP-NOW] Initialized");

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, MOTOR_MAC, 6);
  peer.channel = WIFI_CHANNEL;
  peer.encrypt = false;
  
  if (esp_now_add_peer(&peer) == ESP_OK) {
    Serial.println("[ESP-NOW] Motor peer added");
  } else {
    Serial.println("[ESP-NOW] Failed to add motor peer");
  }

  /* IMU */
  imuReady = bno.begin_I2C(BNO_ADDR);
  if (imuReady) {
    bno.enableReport(SH2_ROTATION_VECTOR);
    Serial.println("[IMU] OK");
  } else {
    Serial.println("[IMU] Not found");
  }

  /* GPS */
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("[GPS] Serial initialized");

  /* HTTP Server */
  server.serveStatic("/", SPIFFS, "/")
        .setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *request){
    String path = request->url();
    if (SPIFFS.exists(path)) {
      request->send(SPIFFS, path);
    } else {
      request->send(404, "text/plain", "Not found: " + path);
    }
  });

  /* WebSocket */
  ws.onEvent(onWS);
  server.addHandler(&ws);

  server.begin();
  Serial.println("[HTTP] Server started on port 80");
  Serial.println("[SETUP] Ready!\n");
}

/* ================= LOOP ================= */
void loop() {
  // GPS update
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  if (gps.location.isValid()) {
    lat = gps.location.lat();
    lng = gps.location.lng();
  }

  // IMU update
  if (imuReady) {
    sh2_SensorValue_t sensorValue;
    if (bno.getSensorEvent(&sensorValue)) {
      if (sensorValue.sensorId == SH2_ROTATION_VECTOR) {
        float qi = sensorValue.un.rotationVector.i;
        float qj = sensorValue.un.rotationVector.j;
        float qk = sensorValue.un.rotationVector.k;
        float qr = sensorValue.un.rotationVector.real;

        yaw = atan2f(2*(qr*qk + qi*qj), 1 - 2*(qj*qj + qk*qk)) * 180.0/PI;
        heading = yaw;
      }
    }
  }

  // TELEMETRY → send to HTML via WebSocket
  StaticJsonDocument<256> doc;
  doc["yaw"]     = yaw;
  doc["heading"] = heading;
  doc["lat"]     = lat;
  doc["lng"]     = lng;
  doc["gpsOk"]   = gps.location.isValid();
  doc["sats"]    = gps.satellites.value();

  String out;
  serializeJson(doc, out);
  ws.textAll(out);

  ws.cleanupClients();

  delay(100);
}
