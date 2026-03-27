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

const char* ssid     = "RoverAP";
const char* password = "rover1234";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

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

// ─── Execute a replay sequence received from dashboard ───────
// Each step: send the move back to dashboard so it redraws,
// then call your motor function here.
// stepDelayMs = pause between moves in milliseconds
void executeReplay(JsonArray moves, int stepDelayMs = 1500) {
  // Tell dashboard replay is starting
  ws.textAll("{\"replay_start\":true}");
  ws.cleanupClients();
  delay(200);

  for (JsonObject move : moves) {
    const char* dir = move["dir"];
    float dist      = move["dist"];

    // ── PUT YOUR MOTOR CODE HERE ──────────────────────────────
    // driveForward(dist);   // if dir == "F"
    // driveBackward(dist);  // if dir == "B"
    // strafeLeft(dist);     // if dir == "L"
    // strafeRight(dist);    // if dir == "R"
    // ─────────────────────────────────────────────────────────

    // Echo the move back so dashboard redraws the trail
    sendMove(dir, dist);
    delay(stepDelayMs);
  }

  // Tell dashboard replay is done
  ws.textAll("{\"replay_done\":true}");
  ws.cleanupClients();
  Serial.println("Replay complete.");
}

// ─── WebSocket event handler ─────────────────────────────────
void onWsEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("Dashboard connected: client #%u\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("Dashboard disconnected: client #%u\n", client->id());
  } else if (type == WS_EVT_DATA) {
    // Receive message from dashboard (e.g. replay command)
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      String msg = "";
      for (size_t i = 0; i < len; i++) msg += (char)data[i];

      DynamicJsonDocument doc(4096);
      DeserializationError err = deserializeJson(doc, msg);
      if (err) { Serial.println("JSON parse error"); return; }

      if (doc.containsKey("replay")) {
        Serial.println("Replay command received.");
        executeReplay(doc["replay"].as<JsonArray>());
      }
    }
  }
}

// ─── Setup ───────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Connect to home WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());   // <── open this IP in your browser

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed — HTML won't serve from flash.");
    // Dashboard can still be opened from file; WS will still work.
  }

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Serve dashboard HTML from SPIFFS
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(SPIFFS, "/rover_dashboard_2.html", "text/html");
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
