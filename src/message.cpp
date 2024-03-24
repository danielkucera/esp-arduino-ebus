#include "main.hpp"
#include <Ebus.h>

static ebus::Ebus bus;

int pushMsgClient(WiFiClient* client, uint8_t B){
    if (client->availableForWrite() >= AVAILABLE_THRESHOLD) {
        const std::vector<uint8_t> sequence = bus.push(B);
        client->write(sequence.data(), sequence.size());
        return 1;
    }
    return 0;
}

void handleMsgClient(WiFiClient* client){
    while (client->available() && Serial.availableForWrite() > 0) {
        // todo
    }
}
