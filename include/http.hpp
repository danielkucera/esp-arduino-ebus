#pragma once

#include <WiFiServer.h>
#include <IotWebConf.h>
#include "store.hpp"

void handleStatus();
#ifdef EBUS_INTERNAL
void handleCommandsList();
void handleCommandsUpload();
void handleCommandsDownload();
void handleCommandsLoad();
void handleCommandsSave();
void handleCommandsWipe();
void handleCommandsInsert();
void handleValues();
#endif
void SetupHttpHandlers();

extern WebServer configServer;
extern IotWebConf iotWebConf;