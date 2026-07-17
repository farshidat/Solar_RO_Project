#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <ArduinoJson.h>

typedef void (*WebServerCommandHandler)(JsonDocument &cmd);

void webServerInit();
void webServerBroadcast(const String &json);

// main.cpp (the message/orchestration layer) registers a handler here to
// receive commands parsed from incoming WebSocket JSON. web_server.cpp stays
// a pure transport layer and knows nothing about pumps/sensors.
void webServerOnCommand(WebServerCommandHandler handler);

#endif // WEB_SERVER_H
