#include "web_server.h"
#include "config.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static WebServerCommandHandler commandHandler = nullptr;

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type != WS_EVT_DATA) return;

  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) {
    return;
  }
  if (!commandHandler) return;

  JsonDocument doc;
  if (deserializeJson(doc, data, len)) return;
  commandHandler(doc);
}

void webServerInit() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);

  if (!LittleFS.begin(true)) {
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
