#include "web_server.h"
#include "config.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <esp_wifi.h>
#include <string.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static WebServerCommandHandler commandHandler = nullptr;
static WebServerStatusBuilder statusBuilder = nullptr;
static uint32_t lastCleanupMs = 0;
static bool mdnsOk = false;
static char gApiStatusBuf[API_STATUS_BUF_SIZE];
static char gApiStatusSend[API_STATUS_BUF_SIZE];  // owned copy for async send
static JsonDocument gWsCmdDoc;

static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GW(192, 168, 4, 1);
static const IPAddress AP_MASK(255, 255, 255, 0);

static void startMdns() {
  if (mdnsOk) {
    MDNS.end();
    mdnsOk = false;
  }
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    mdnsOk = true;
  }
}

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

  gWsCmdDoc.clear();
  if (deserializeJson(gWsCmdDoc, data, len)) return;
  commandHandler(gWsCmdDoc);  // must return immediately (no sensor waits)
}

void webServerInit() {
  WiFi.mode(WIFI_OFF);
  delay(50);  // setup only — AP mode transition
  WiFi.mode(WIFI_AP);
  WiFi.setHostname(MDNS_HOSTNAME);

  WiFi.softAPConfig(AP_IP, AP_GW, AP_MASK);
  WiFi.setSleep(false);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONN);
  delay(100);  // setup only

  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_max_tx_power(WIFI_AP_TX_POWER_QDB);

  startMdns();

  if (!LittleFS.begin(true)) {
    Serial.println("[web] LittleFS mount failed");
    return;
  }

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!statusBuilder) {
      request->send(503, "application/json", "{\"error\":\"status unavailable\"}");
      return;
    }
    size_t n = statusBuilder(gApiStatusBuf, sizeof(gApiStatusBuf));
    if (n == 0 || n >= sizeof(gApiStatusBuf)) {
      request->send(500, "application/json", "{\"error\":\"status build failed\"}");
      return;
    }
    // Copy into dedicated send buffer (no Arduino String / heap fragment)
    memcpy(gApiStatusSend, gApiStatusBuf, n);
    gApiStatusSend[n] = '\0';
    AsyncWebServerResponse *res =
        request->beginResponse(200, "application/json", (const uint8_t *)gApiStatusSend, n);
    res->addHeader("Cache-Control", "no-store");
    request->send(res);
  });

  // Static assets: prefer .gz when present (upload gzipped siblings via data/).
  // HTML short-lived; JS/CSS longer cache — SW still busts via CACHE_NAME.
  server.serveStatic("/", LittleFS, "/")
      .setCacheControl("public, max-age=86400")
      .setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });

  server.begin();
  lastCleanupMs = millis();

  Serial.printf("[web] AP SSID=%s ch=%u IP=%s mDNS=%s.local (no captive)\n",
                WIFI_AP_SSID, (unsigned)WIFI_AP_CHANNEL, AP_IP.toString().c_str(),
                MDNS_HOSTNAME);
}

void webServerLoop() {
  yield();
  webServerCleanupClients();
}

bool webServerCaptivePortalActive() { return false; }

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
  (void)len;
  ws.textAll(json);
}

void webServerOnCommand(WebServerCommandHandler handler) {
  commandHandler = handler;
}

void webServerOnStatus(WebServerStatusBuilder builder) {
  statusBuilder = builder;
}
