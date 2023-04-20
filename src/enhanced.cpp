#include <WiFiClient.h>
#include "main.hpp"
#include "ebusstate.hpp"
#include "arbitration.hpp"
#include "enhanced.hpp"

#define M1 0b11000000
#define M2 0b10000000

#define ARBITRATION_TIMEOUT_MS 2000

// Locking
#ifdef USE_ASYNCHRONOUS
SemaphoreHandle_t getMutex(){
    static SemaphoreHandle_t _lock = NULL;
    if(_lock == NULL){
        _lock = xSemaphoreCreateMutex();
        if(_lock == NULL){
            DEBUG_LOG("xSemaphoreCreateMutex failed");
            return NULL;
        }
    }
    return _lock;
}
#define ENH_MUTEX_LOCK()    do {} while (xSemaphoreTake(getMutex(), portMAX_DELAY) != pdPASS)
#define ENH_MUTEX_UNLOCK()  xSemaphoreGive(getMutex())
#else
#define ENH_MUTEX_LOCK()    
#define ENH_MUTEX_UNLOCK()  
#endif


WiFiClient*   _arbitration_client = NULL;
int           _arbitration_address = -1;
unsigned long arbitration_start = 0;

void getEnhArbitrationClient(WiFiClient* &client, uint8_t &address) {
    ENH_MUTEX_LOCK();
    client = _arbitration_client;
    address= _arbitration_address;
    ENH_MUTEX_UNLOCK();
}

void clearEnhArbitrationClient() {
    ENH_MUTEX_LOCK();
    _arbitration_client = 0;
    _arbitration_address= -1;
    ENH_MUTEX_UNLOCK();
}

bool setEnhArbitrationClient(WiFiClient* &client, uint8_t &address) {
    bool result = true;
    ENH_MUTEX_LOCK();
    if (_arbitration_client == NULL) {
        _arbitration_client = client;
        _arbitration_address = address;
    }
    else {
        result = false;
        client = _arbitration_client;
        address = _arbitration_address;
    }
    ENH_MUTEX_UNLOCK();
    return result;
}

void decode(int b1, int b2, uint8_t (&data)[2]){
    data[0] = (b1 >> 2) & 0b1111;
    data[1] = ((b1 & 0b11) << 6) | (b2 & 0b00111111);
}

void encode(uint8_t c, uint8_t d, uint8_t (&data)[2]){
    data[0] = M1 | c << 2 | d >> 6;
    data[1] = M2 | (d & 0b00111111);
}

void send_res(WiFiClient* client, uint8_t c, uint8_t d){
    uint8_t data[2];
    encode(c, d, data);
    client->write(data, 2);
}

void process_cmd(WiFiClient* client, uint8_t c, uint8_t d){
    if (c == CMD_INIT){
        send_res(client, RESETTED, 0x0);
        return;
    }
    if (c == CMD_START){
        if (d == SYN){
            //arbitration_client = NULL;
            clearEnhArbitrationClient();
            DEBUG_LOG("CMD_START SYN\n");
            return;
        } else {
            // start arbitration
            WiFiClient* cl = client;
            uint8_t     ad = d;
            if (!setEnhArbitrationClient(client, d) ) {
                if (cl!=client) {
                    // only one client can be in arbitration
                    DEBUG_LOG("CMD_START ONGOING 0x%02 0x%02x\n", ad, d);
                    send_res(client, ERROR_HOST, ERR_FRAMING);
                    return;
                }
                else {
                    DEBUG_LOG("CMD_START REPEAT 0x%02x\n", d);
                }
            }
            else {
                DEBUG_LOG("CMD_START 0x%02x\n", d);
            }
            //arbitration_client = client;
            //arbitration_address = d;            
            setEnhArbitrationClient(client, d);
            arbitration_start = millis();
            return;
        }
    }
    if (c == CMD_SEND){
        DEBUG_LOG("SEND 0x%02x\n", d);
        Serial.write(d);
        return;
    }
    if (c == CMD_INFO){
        // if needed, set bit 0 as reply to INIT command
        return;
    }
}

bool read_cmd(WiFiClient* client, uint8_t (&data)[2]){
    int b, b2;

    b = client->read();

    if (b<0){
        // available and read -1 ???
        return false;
    }

    if (b<0b10000000){
        data[0] = CMD_SEND;
        data[1] = b;
        return true;
    }

    if (b<0b11000000){
        DEBUG_LOG("first command missing\n");
        client->write("first command missing");
        // first command signature error
        client->stop();
        return false;
    }

    b2 = client->read();

    if (b2<0) {
        // second command missing
        DEBUG_LOG("second command missing\n");
        client->write("second command missing");
        client->stop();
        return false;
    }

    if ((b2 & 0b11000000) != 0b10000000){
        // second command signature error
        DEBUG_LOG("second command signature error\n");
        client->write("second command signature error");
        client->stop();
        return false;
    }

    decode(b, b2, data);
    return true;
}

void handleEnhClient(WiFiClient* client){
    while (client->available()) {
        uint8_t data[2];
        if (read_cmd(client, data)) {
            process_cmd(client, data[0], data[1]);
        }
    }
}

int pushEnhClient(WiFiClient* client, uint8_t c, uint8_t d, bool log){

    if (log) {
        DEBUG_LOG("DATA           0x%02x 0x%02x\n", c, d);
    }
    if (client->availableForWrite() >= AVAILABLE_THRESHOLD) {
        send_res(client, c,  d);
        return 1;
    }
    return 0;
}

void enhArbitrationDone() {
    clearEnhArbitrationClient();
}

WiFiClient* enhArbitrationRequested(uint8_t& aa) {
    WiFiClient* client = NULL;
    getEnhArbitrationClient(client, aa);
    return client;
}
