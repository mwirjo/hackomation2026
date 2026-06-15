#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>

// Default home Wi-Fi credentials (Change these to yours!)
String mySSID = "Your_Home_WiFi_Name";
String myPASS = "Your_Home_WiFi_Password";

// Fallback AP credentials
const char* ap_ssid = "EVE_Backup_Net";
const char* ap_password = "password123";

AsyncWebServer server(80);

void startWebServer() {
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "File not found on ESP32 storage");
  });
  server.begin();
  Serial.println("[HTTP] Server active on port 80");
}

void initWiFi() {
  WiFi.disconnect(true);
  delay(100);
  
  Serial.printf("\n[WiFi] Attempting to connect to Station: %s...\n", mySSID.c_str());
  WiFi.mode(WIFI_AP_STA); // Enable both Station and Access Point modes
  WiFi.begin(mySSID.c_str(), myPASS.c_str());

  // Wait up to 10 seconds for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected successfully!");
    Serial.print("[WiFi] Dashboard local IP URL: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] Connection timed out. Starting Backup Access Point...");
    WiFi.softAP(ap_ssid, ap_password);
    Serial.print("[WiFi] Backup AP active. Connect to 'EVE_Backup_Net' and open: http://");
    Serial.println(WiFi.softAPIP());
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== E.V.E Network Hybrid Node Starting ===");

  // Mount Filesystem
  if (!SPIFFS.begin(true)) {
    Serial.println("[FS] SPIFFS Mount Failed!");
    return;
  }
  Serial.println("[FS] SPIFFS Mounted cleanly.");

  // Start network and server
  initWiFi();
  startWebServer();
  
  Serial.println("\n[SYSTEM] Ready. To change Wi-Fi via Serial, type: wifi <SSID> <PASSWORD>");
}

void loop() {
  // Check for dynamic Serial commands to change Wi-Fi networks on the fly
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.startsWith("wifi ")) {
      // Format: "wifi SSID PASSWORD"
      String configStr = input.substring(5);
      int spaceIdx = configStr.indexOf(' ');
      
      if (spaceIdx > 0) {
        mySSID = configStr.substring(0, spaceIdx);
        myPASS = configStr.substring(spaceIdx + 1);
        
        Serial.printf("\n[SYSTEM] Received new credentials! Switching to: %s\n", mySSID.c_str());
        initWiFi();
      } else {
        Serial.println("[SYSTEM] Invalid format. Use: wifi <SSID> <PASSWORD>");
      }
    }
  }
}