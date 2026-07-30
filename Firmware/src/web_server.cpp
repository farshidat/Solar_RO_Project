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

/** Serve the Web App — Android/iOS treat non-204 / non-"Success" as captive portal. */
static void servePortalApp(AsyncWebServerRequest *request) {
  if (LittleFS.exists("/index.html")) {
    request->send(LittleFS, "/index.html", "text/html");
  } else {
    request->send(200, "text/html",
                  "<!DOCTYPE html><html><body><h1>Nik-Sun-Purifier</h1>"
                  "<p>Open <a href=\"/\">Web App</a></p></body></html>");
  }
}

static void redirectToApRoot(AsyncWebServerRequest *request) {
  char loc[32];
  snprintf(loc, sizeof(loc), "http://%u.%u.%u.%u/", AP_IP[0], AP_IP[1], AP_IP[2], AP_IP[3]);
  AsyncWebServerResponse *res = request->beginResponse(302, "text/plain", "");
  res->addHeader("Location", loc);
  res->addHeader("Cache-Control", "no-cache");
  request->send(res);
}

/**
 * Captive catch-all (ESPAsyncWebServer official pattern).
 * Registered after real file/WS routes; canHandle=true only if not a known asset.
 */
class CaptiveRequestHandler : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    if (!captiveActive) return false;
    // Let WebSocket upgrade alone
    if (request->url() == "/ws") return false;
    const String &url = request->url();
    // Known app assets — leave to serveStatic / explicit routes
    if (url == "/" || url == "/index.html") return false;
    if (url.endsWith(".js") || url.endsWith(".css") || url.endsWith(".svg") ||
        url.endsWith(".png") || url.endsWith(".ico") || url.endsWith(".json") ||
        url.endsWith(".woff") || url.endsWith(".woff2") || url.endsWith(".map")) {
      return false;
    }
    return true;
  }

  void handleRequest(AsyncWebServerRequest *request) override {
    // Prefer serving the app HTML so the OS captive sheet shows content immediately
    servePortalApp(request);
  }
};

static CaptiveRequestHandler captiveHandler;

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

static void registerCaptiveProbeRoutes() {
  // Android — must NOT return HTTP 204 (that means "internet OK", no popup)
  auto androidProbe = [](AsyncWebServerRequest *request) { servePortalApp(request); };
  server.on("/generate_204", HTTP_GET, androidProbe);
  server.on("/gen_204", HTTP_GET, androidProbe);
  server.on("/generate204", HTTP_GET, androidProbe);

  // Apple / iOS — body must not be the word Success alone
  server.on("/hotspot-detect.html", HTTP_GET, androidProbe);
  server.on("/library/test/success.html", HTTP_GET, androidProbe);

  // Windows NCSI
  server.on("/connecttest.txt", HTTP_GET, androidProbe);
  server.on("/ncsi.txt", HTTP_GET, androidProbe);
  server.on("/redirect", HTTP_GET, androidProbe);
  server.on("/fwlink/", HTTP_GET, androidProbe);
  server.on("/fwlink", HTTP_GET, androidProbe);

  // Kindle / misc
  server.on("/canonical.html", HTTP_GET, androidProbe);
  server.on("/success.txt", HTTP_GET, androidProbe);
}

void webServerInit() {
  WiFi.mode(WIFI_OFF);
  delay(50);
  WiFi.mode(WIFI_AP);
  WiFi.setHostname(MDNS_HOSTNAME);

  // Critical: gateway = AP IP so DHCP hands out this DNS for captive detection
  WiFi.softAPConfig(AP_IP, AP_GW, AP_MASK);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, 1 /*channel*/, 0 /*not hidden*/, 4);
  delay(200);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  // Newer cores: mark DHCP as captive-portal network
  WiFi.AP.enableDhcpCaptivePortal();
#endif

  // Wildcard DNS → SoftAP IP
  dnsServer.setTTL(CAPTIVE_PORTAL_TTL);
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(DNS_PORT, "*", AP_IP);
  captiveActive = true;

  startMdns();

  if (!LittleFS.begin(true)) {
    Serial.println("[web] LittleFS mount failed");
    return;
  }

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Probe routes BEFORE static (must win over any filesystem miss)
  registerCaptiveProbeRoutes();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    servePortalApp(request);
  });

  // Real assets
  server.serveStatic("/", LittleFS, "/")
      .setCacheControl("no-cache")
      .setDefaultFile("index.html");

  // Official Async pattern: AP-only captive catch-all
  server.addHandler(&captiveHandler).setFilter(ON_AP_FILTER);

  server.onNotFound([](AsyncWebServerRequest *request) {
    if (captiveActive) {
      // Still show app (popup content) rather than empty 404
      servePortalApp(request);
      return;
    }
    redirectToApRoot(request);
  });

  server.begin();
  lastCleanupMs = millis();

  Serial.printf("[web] AP SSID=%s IP=%s mDNS=%s.local captive=on\n",
                WIFI_AP_SSID, AP_IP.toString().c_str(), MDNS_HOSTNAME);
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
