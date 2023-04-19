#include <WiFiClient.h>
#include "main.hpp"
#include "ebusstate.hpp"
#include "arbitration.hpp"
#include "enhanced.hpp"

#define M1 0b11000000
#define M2 0b10000000

#define ARBITRATION_TIMEOUT_MS 2000



WiFiClient* arbitration_client = NULL;
unsigned long arbitration_start = 0;
int arbitration_address = -1;

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
            arbitration_client = NULL;
            DEBUG_LOG("CMD_START SYN\n");
            return;
        } else {
            // start arbitration
            if (arbitration_client ) {
                if (arbitration_client!=client) {
                    // only one client can be in arbitration
                    DEBUG_LOG("CMD_START ONGOING 0x%02 0x%02x\n", arbitration_address, d);
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

            arbitration_client = client;
            arbitration_start = millis();
            arbitration_address = d;

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
        client->write("first command signature error");
        // first command signature error
        client->stop();
        return false;
    }

    b2 = client->read();

    if (b2<0) {
        // second command missing
        client->write("second command missing");
        client->stop();
        return false;
    }

    if ((b2 & 0b11000000) != 0b10000000){
        // second command signature error
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

int pushEnhClient(WiFiClient* client, uint8_t c, uint8_t d){
    if (client->availableForWrite() >= AVAILABLE_THRESHOLD) {
        send_res(client, c,  d);
        return 1;
    }
    return 0;
}

void enhArbitrationDone(WiFiClient* client) {
    // lock
    arbitration_client = NULL;
    // unlock
}

WiFiClient* enhArbitrationRequested(uint8_t& aa) {
    // lock
    aa = arbitration_address;
    return arbitration_client;
    // unlock
}



/*
size_t arbitrateEnhClient(WiFiClient* client, EBusState& busstate, uint8_t* bytes){
    size_t bytesread = 0;
    if (client->availableForWrite() >= AVAILABLE_THRESHOLD && arbitration_client == client) {
        Arbitration arbitration;
        if (arbitration.start(busstate, arbitration_address) ) {
            int loopcount = 0;
            while (loopcount++ < ARBITRATION_BUFFER_SIZE){
                while (Serial.available() == 0) {
                    if (millis() > arbitration_start + ARBITRATION_TIMEOUT_MS) {
                        DEBUG_LOG("ARB TIMEOUT 1 0x%02x 0x%02x\n", busstate._master, busstate._byte);
                        send_res(client, ERROR_EBUS, ERR_FRAMING);
                        arbitration_client = NULL;
                        return bytesread;
                    }
                }
                
                uint8_t symbol = Serial.read();
                DEBUG_LOG("ARB SYMBOL     0x%02x %ld us\n", symbol, busstate.microsSinceLastSyn());
                busstate.data(symbol);
                bytes[bytesread++] = symbol;
                switch (arbitration.data(busstate, symbol)) {
                    case Arbitration::none:
                        arbitration_client = NULL;
                        return bytesread;
                    case Arbitration::arbitrating:
                        break;
                    case Arbitration::won:
                        DEBUG_LOG("ARB SEND WON   0x%02x %ld us\n", busstate._master, busstate.microsSinceLastSyn());
                        send_res(client, STARTED, busstate._master);
                        arbitration_client = NULL;
                        return bytesread;
                    case Arbitration::lost:
                        DEBUG_LOG("ARB SEND LOST  0x%02x 0x%02x %ld us\n", busstate._master, busstate._byte, busstate.microsSinceLastSyn());
                        send_res(client, FAILED, busstate._master);
                        send_res(client, RECEIVED, busstate._byte);
                        arbitration_client = NULL;
                        return bytesread;
                    case Arbitration::error:
                        arbitration_client = NULL;
                        return bytesread;
                }
            }
            arbitration_client = NULL;     
        }
        if (arbitration_client && (millis() > arbitration_start + ARBITRATION_TIMEOUT_MS)){
            DEBUG_LOG("ARB TIMEOUT 2 0x%02x 0x%02x\n", busstate._master, busstate._byte);
            send_res(client, ERROR_EBUS, ERR_FRAMING);
            arbitration_client = NULL;
        }
    }
    return bytesread;
}
*/