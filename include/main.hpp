#ifndef _MAIN_HPP_
#define _MAIN_HPP_

#include <WiFiClient.h>
#include <WiFiServer.h>

#define MAX_SRV_CLIENTS 4
#define RXBUFFERSIZE 1024
#define ARBITRATION_BUFFER_SIZE 20
#define STACK_PROTECTOR  512 // bytes
#define HOSTNAME "esp-eBus"
#define RESET_MS 1000

#ifdef ESP32
// https://esp32.com/viewtopic.php?t=19788
#define AVAILABLE_THRESHOLD 0
#else
#define AVAILABLE_THRESHOLD 1
#endif

//inline int DEBUG_LOG(const char *format, ...) { return 0;}
int DEBUG_LOG_IMPL(const char *format, ...);
#define DEBUG_LOG DEBUG_LOG_IMPL

bool handleNewClient(WiFiServer &server, WiFiClient clients[]);
int pushClient(WiFiClient* client, uint8_t B);
void handleClient(WiFiClient* client);

class EBusState;

size_t arbitrateEnhClient(WiFiClient* client, EBusState& busstate, uint8_t* bytes);
int    pushEnhClient(WiFiClient* client, uint8_t B);
void   handleEnhClient(WiFiClient* client);

#endif