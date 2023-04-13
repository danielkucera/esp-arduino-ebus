#ifndef _MAIN_HPP_
#define _MAIN_HPP_

#include <WiFiClient.h>
#include <WiFiServer.h>

#define MAX_SRV_CLIENTS 4
#define RXBUFFERSIZE 1024
#define ARB_CLIENT_BUFFER_SIZE 20
#define STACK_PROTECTOR  512 // bytes
#define HOSTNAME "esp-eBus"
#define RESET_PIN 5
#define RESET_MS 1000

#ifndef TX_DISABLE_PIN
#define TX_DISABLE_PIN 5
#endif

#ifdef ESP32
// https://esp32.com/viewtopic.php?t=19788
#define AVAILABLE_THRESHOLD 0
#else
#define AVAILABLE_THRESHOLD 1
#endif

bool handleNewClient(WiFiServer &server, WiFiClient clients[]);
int pushClient(WiFiClient* client, uint8_t B);
void handleClient(WiFiClient* client);

class EBusState;
int    pushEnhClient(WiFiClient* client, uint8_t B);
size_t arbitrateEnhClient(WiFiClient* client, EBusState& busstate, uint8_t* bytes);
void   handleEnhClient(WiFiClient* client);

int logmessage(const char *format, ...);

#endif