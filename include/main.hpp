#ifndef _MAIN_HPP_
#define _MAIN_HPP_

#include <WiFiClient.h>
#include <WiFiServer.h>

#define MAX_SRV_CLIENTS 4
#define RXBUFFERSIZE 1024
#define QUEUE_SIZE 480 
#define STACK_PROTECTOR  512 // bytes
#define HOSTNAME "esp-eBus"
#define RESET_MS 1000


#ifdef ESP32
#define USE_ASYNCHRONOUS 1
// https://esp32.com/viewtopic.php?t=19788
#define AVAILABLE_THRESHOLD 0
#else
#define USE_ASYNCHRONOUS 0
#define AVAILABLE_THRESHOLD 1
#endif

inline int DEBUG_LOG(const char *format, ...) { return 0;}
//int DEBUG_LOG_IMPL(const char *format, ...);
//#define DEBUG_LOG DEBUG_LOG_IMPL

bool handleNewClient(WiFiServer &server, WiFiClient clients[]);
int  pushClient(WiFiClient* client, uint8_t B);
void handleClient(WiFiClient* client);


#endif
