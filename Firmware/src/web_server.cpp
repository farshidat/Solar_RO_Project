#include "web_server.h"
#include "config.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <string.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static WebServerCommandHandler commandHandler = nullptr;
static uint32_t lastCleanupMs = 0;

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  (void)server;
  (void)client;
  if (type != WS_EVT_DATA) return;

  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)) {
    return;
  }
  if (!commandHandler) return;

  // Stack-friendly: commands are small (power / scenario / reset / calib)
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
  lastCleanupMs = millis();
}

bool webServerHasClients() { return ws.count() > 0; }

void webServerCleanupClients() {
  const uint32_t now = millis();
  if ((now - lastCleanupMs) < WS_CLEANUP_MS) return;
  lastCleanupMs = now;
  ws.cleanupClients();
}

void webServerBroadcast(const char *json) {
  if (!json) return;
  webServerBroadcast(json, strlen(json));
}

void webServerBroadcast(const char *json, size_t len) {
  if (!json || len == 0) return;
  webServerCleanupClients();
  if (ws.count() == 0) return;
  // Ensure NUL for APIs that expect C-string; buffer from main is NUL-capped
  (void)len;
  ws.textAll(json);
}

void webServerOnCommand(WebServerCommandHandler handler) {
  commandHandler = handler;
}
