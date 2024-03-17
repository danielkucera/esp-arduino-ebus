#ifndef _MESSAGE_H_
#define _MESSAGE_H_
#include <WiFiClient.h>

int  pushMsgClient(WiFiClient* client, uint8_t B);
void handleMsgClient(WiFiClient* client);

#endif