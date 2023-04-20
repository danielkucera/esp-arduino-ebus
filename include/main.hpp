#include <WiFiClient.h>
#include <WiFiServer.h>

#define MAX_SRV_CLIENTS 4
#define RXBUFFERSIZE 1024
#define STACK_PROTECTOR  512 // bytes
#define HOSTNAME "esp-eBus"
#define RESET_MS 1000

#ifdef ESP32
// https://esp32.com/viewtopic.php?t=19788
#define AVAILABLE_THRESHOLD 0
#else
#define AVAILABLE_THRESHOLD 1
#endif

bool handleNewClient(WiFiServer &server, WiFiClient clients[]);
int pushClient(WiFiClient* client, uint8_t B);
void handleClient(WiFiClient* client);

int pushEnhClient(WiFiClient* client, uint8_t B);
void handleEnhClient(WiFiClient* client);

int pushMsgClient(WiFiClient* client, uint8_t B);
void handleMsgClient(WiFiClient* client);