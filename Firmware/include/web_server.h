#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ArduinoJson.h>

typedef void (*WebServerCommandHandler)(JsonDocument &cmd);

void webServerInit();

/** Broadcast UTF-8 JSON to all WS clients. Prefer C-string to avoid String heap churn. */
void webServerBroadcast(const char *json);
void webServerBroadcast(const char *json, size_t len);

bool webServerHasClients();
void webServerCleanupClients();

// main.cpp registers command handler; web_server stays transport-only.
void webServerOnCommand(WebServerCommandHandler handler);

#endif // WEB_SERVER_H
