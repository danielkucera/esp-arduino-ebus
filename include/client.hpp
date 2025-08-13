#pragma once

#include <WiFiClient.h>
#include <WiFiServer.h>

bool handleNewClient(WiFiServer *server, WiFiClient clients[]);

void handleClient(WiFiClient *client);
int pushClient(WiFiClient *client, uint8_t B);

void handleEnhClient(WiFiClient *client);
int pushEnhClient(WiFiClient *client, uint8_t c, uint8_t d, bool log);
