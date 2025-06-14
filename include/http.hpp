#pragma once

#include <IotWebConf.h>
#include <WiFiServer.h>

void SetupHttpHandlers();

extern WebServer configServer;
extern IotWebConf iotWebConf;
