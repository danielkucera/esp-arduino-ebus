#pragma once

#include <WiFiServer.h>
#include <IotWebConf.h>
#include "store.hpp"

void SetupHttpHandlers();

extern WebServer configServer;
extern IotWebConf iotWebConf;