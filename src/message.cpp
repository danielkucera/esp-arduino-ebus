#include "main.hpp"
#include "message.hpp"
#include <Ebus.h>

ebus::Ebus bus;
int messageCounter = 0;

int pushMsgClient(WiFiClient* client, uint8_t byte) {
    if (client->availableForWrite() >= AVAILABLE_THRESHOLD) {
        const std::vector<uint8_t> sequence = bus.push_read(byte);
        client->write(sequence.data(), sequence.size());

        // if (sequence.size() > 0)
        //     client->write(bus.to_string_tel_read().c_str());

        return 1;
    }
    return 0;
}

void handleMsgClient(WiFiClient* client) { 
    while (client->available()) {
        messageCounter++;

        uint8_t msgBuffer[16];
        memset(&msgBuffer[0], 0, sizeof(msgBuffer));

        size_t msgSize = client->read(&msgBuffer[0], sizeof(msgBuffer));

        if (msgSize > 0) {
            bus.clear_write();

            for (size_t i = 0; i < msgSize; i++)
                bus.push_write(msgBuffer[i]);

            client->write(printMessage());
            client->stop();
        }
    }
}

char* printMessage() {
    static char message[34];
    sprintf(message, "%s\r\n", bus.to_string_seq_write().c_str());
    return message;
}

int printMessageCounter() {
    return messageCounter;
}
