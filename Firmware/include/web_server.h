#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ArduinoJson.h>

typedef void (*WebServerCommandHandler)(JsonDocument &cmd);
/** Fill buf with JSON; return length (0 on error). Used by GET /api/status. */
typedef size_t (*WebServerStatusBuilder)(char *buf, size_t cap);

/** SoftAP + mDNS + AsyncWebServer + WebSocket. */
void webServerInit();

/** Call every loop tick (DNSServer.processNextRequest). Non-blocking. */
void webServerLoop();

/** Broadcast UTF-8 JSON to all WS clients. Prefer C-string to avoid String heap churn. */
void webServerBroadcast(const char *json);
void webServerBroadcast(const char *json, size_t len);

bool webServerHasClients();
void webServerCleanupClients();

void webServerOnCommand(WebServerCommandHandler handler);
void webServerOnStatus(WebServerStatusBuilder builder);

/** True while SoftAP captive DNS is running. */
bool webServerCaptivePortalActive();

#endif // WEB_SERVER_H
