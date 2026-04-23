/*
  rover_dashboard.ino
  Sends move commands to the dashboard as: {"dir":"F","dist":1.0}
  dir  = F (forward) | B (backward) | L (left) | R (right)
  dist = metres (float)

  Serial commands (115200 baud):
    F1.0    → forward 1.0 m
    B0.5    → backward 0.5 m
    L0.25   → left 0.25 m
    R2.0    → right 2.0 m
    reset   → reset rover to origin on dashboard

  In your real rover code, call sendMove("F", distanceInMetres)
  after each movement completes.
*/

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <AsyncWebSocket.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

const char* ssid     = "HUAWEI-2.4G-pM7X";
const char* password = "GDm6PwGD";
const char* ap_ssid     = "HUAWEI-2.4G-pM7X";
const char* ap_password = "GDm6PwGD";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
void initWifi() {
  // --- 1. Try connecting to home WiFi (STA mode) ---
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  unsigned long startAttemptTime = millis();
  const unsigned long WIFI_TIMEOUT_MS = 10000; // 10 second timeout

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttemptTime < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  // --- 2. Fall back to AP mode if STA failed ---
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("STA failed — starting Access Point...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password);

    Serial.print("AP started! SSID: ");
    Serial.println(ap_ssid);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP()); // default: 192.168.4.1
  } else {
    Serial.print("Connected! IP: ");
    Serial.println(WiFi.localIP());
  }

  // --- 3. Mount SPIFFS ---
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed — HTML won't serve from flash.");
  }
}

// ─── Send one move packet to the dashboard ───────────────────
// dir  : "F" | "B" | "L" | "R"
// dist : metres moved (positive float)
void sendMove(const char* dir, float dist) {
  if (ws.count() == 0) return;   // no clients connected, skip

  StaticJsonDocument<64> doc;
  doc["dir"]  = dir;
  doc["dist"] = dist;

  String out;
  serializeJson(doc, out);
  ws.textAll(out);
  ws.cleanupClients();

  Serial.printf("Sent: dir=%s dist=%.3f\n", dir, dist);
}

// ─── Parse Serial commands ───────────────────────────────────
// Format: <DIR><DIST>\n   e.g. "F1.0", "R0.5", "reset"
void parseSerial() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (!cmd.length()) return;

  Serial.println(">> " + cmd);

  if (cmd.equalsIgnoreCase("reset")) {
    // Tell dashboard to clear/reset — send a special packet
    if (ws.count() > 0) {
      ws.textAll("{\"reset\":true}");
      ws.cleanupClients();
    }
    Serial.println("Reset sent to dashboard.");
    return;
  }

  // First char is direction, rest is distance
  char dirChar = toupper(cmd.charAt(0));
  float dist   = cmd.substring(1).toFloat();

  if ((dirChar == 'F' || dirChar == 'B' || dirChar == 'L' || dirChar == 'R') && dist > 0) {
    char dirStr[2] = { dirChar, '\0' };
    sendMove(dirStr, dist);
  } else {
    Serial.println("Unknown command.");
    Serial.println("Usage: F1.0 | B0.5 | L0.25 | R2.0 | reset");
  }
}

// ─── WebSocket event handler ─────────────────────────────────
void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT)
    Serial.printf("Dashboard connected: client #%u\n", client->id());
  else if (type == WS_EVT_DISCONNECT)
    Serial.printf("Dashboard disconnected: client #%u\n", client->id());
}

// ─── Setup ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Connect to home WiFi
  initWifi();

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Serve dashboard HTML from SPIFFS
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(SPIFFS, "/rover_dashboard.html", "text/html");
  });

  server.begin();
  Serial.println("Server ready.");
  Serial.println("Serial commands: F1.0 | B0.5 | L0.25 | R2.0 | reset");
}

// ─── Loop ────────────────────────────────────────────────────
void loop() {
  parseSerial();
  // No polling — only send when a move actually happens.
  // If you drive your rover from here, call sendMove() after each move:
  //   sendMove("F", 1.0);
  //   sendMove("R", 0.5);
}
