#include "web_server.h"
#include "config.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <string.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static DNSServer dnsServer;
static WebServerCommandHandler commandHandler = nullptr;
static uint32_t lastCleanupMs = 0;
static bool captiveActive = false;
static bool mdnsOk = false;

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

static void captiveRedirect(AsyncWebServerRequest *request) {
  // Absolute URL is more reliable for OS captive-portal probes
  IPAddress ip = WiFi.softAPIP();
  char loc[40];
  snprintf(loc, sizeof(loc), "http://%u.%u.%u.%u/", ip[0], ip[1], ip[2], ip[3]);
  request->redirect(loc);
}

static bool looksLikeCaptiveProbe(const String &url) {
  // Common Android / iOS / Windows / Kindle connectivity checks
  if (url.indexOf("generate_204") >= 0) return true;
  if (url.indexOf("gen_204") >= 0) return true;
  if (url.indexOf("hotspot-detect") >= 0) return true;
  if (url.indexOf("canonical.html") >= 0) return true;
  if (url.indexOf("connecttest") >= 0) return true;
  if (url.indexOf("ncsi") >= 0) return true;
  if (url.indexOf("success.txt") >= 0) return true;
  if (url.indexOf("captive") >= 0) return true;
  if (url.indexOf("gstatic") >= 0) return true;
  if (url == "/fwlink/" || url.indexOf("fwlink") >= 0) return true;
  return false;
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

  JsonDocument doc;
  if (deserializeJson(doc, data, len)) return;
  commandHandler(doc);
}

void webServerInit() {
  WiFi.mode(WIFI_AP);
  WiFi.setHostname(MDNS_HOSTNAME);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  delay(100);

  IPAddress apIP = WiFi.softAPIP();
  // Wildcard DNS → SoftAP IP so phones open captive portal
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", apIP);
  captiveActive = true;

  startMdns();

  if (!LittleFS.begin(true)) {
    return;
  }

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Root + static assets from LittleFS
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

  // Captive portal: unknown / probe URLs → 302 to Web App root
  server.onNotFound([](AsyncWebServerRequest *request) {
    const String url = request->url();
    if (request->method() == HTTP_GET || request->method() == HTTP_HEAD) {
      if (looksLikeCaptiveProbe(url) || captiveActive) {
        captiveRedirect(request);
        return;
      }
    }
    // Fallback: still send users to the app in AP mode
    if (captiveActive) {
      captiveRedirect(request);
      return;
    }
    request->send(404, "text/plain", "Not found");
  });

  server.begin();
  lastCleanupMs = millis();
}

void webServerLoop() {
  if (captiveActive) {
    dnsServer.processNextRequest();
  }
  webServerCleanupClients();
}

bool webServerCaptivePortalActive() { return captiveActive; }

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
