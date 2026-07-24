#include "web_server.h"
#include "config.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static WebServerCommandHandler commandHandler = nullptr;

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WebSocket client #%u connected\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) {
      return; // ignore fragmented/binary frames, not needed for our small JSON commands
    }
    if (!commandHandler) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) {
      Serial.printf("WS command JSON parse error: %s\n", err.c_str());
      return;
    }
    commandHandler(doc);
  }
}

void webServerInit() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  Serial.print("Access Point IP: ");
  Serial.println(WiFi.softAPIP());

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    return;
  }

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.begin();
}

void webServerBroadcast(const String &json) {
  ws.cleanupClients();
  if (ws.count() > 0) {
    ws.textAll(json);
  }
}

void webServerOnCommand(WebServerCommandHandler handler) {
  commandHandler = handler;
}
