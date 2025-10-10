#pragma once

#include <WiFiClient.h>
#include <WiFiServer.h>

bool handleNewClient(WiFiServer *server, WiFiClient clients[]);

void handleClient(WiFiClient *client);
int pushClient(WiFiClient *client, uint8_t byte);

void handleClientEnhanced(WiFiClient *client);
int pushClientEnhanced(WiFiClient *client, uint8_t c, uint8_t d, bool log);
