#include <WiFiClient.h>
#include "main.hpp"
#include "ebusstate.hpp"

#define M1 0b11000000
#define M2 0b10000000

#define ARBITRATION_TIMEOUT_MS 2000

enum requests {
    CMD_INIT = 0,
    CMD_SEND,
    CMD_START,
    CMD_INFO
};

enum responses {
    RESETTED = 0x0,
    RECEIVED = 0x1,
    STARTED = 0x2,
    INFO = 0x3,
    FAILED = 0xa,
    ERROR_EBUS = 0xb,
    ERROR_HOST = 0xc
};

WiFiClient* arbitration_client;
unsigned long arbitration_start;
int arbitration_address;

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

void send_received(WiFiClient* client, uint8_t byte){
    if (byte< 0x80){
        client->write(byte);
    } else {
        send_res(client, RECEIVED, byte);
    }
}

void process_cmd(WiFiClient* client, uint8_t c, uint8_t d){
    if (c == CMD_INIT){
        send_res(client, RESETTED, 0x0);
        return;
    }
    if (c == CMD_START){
        if (d == SYN){
            arbitration_client = NULL;
            send_res(client, FAILED, 0x3f);
            return;
        } else {
            // start arbitration

            if (arbitration_client) {
                // only one client can be in arbitration
                send_res(client, FAILED, 0x3f);
                return;
            }

            arbitration_client = client;
            arbitration_start = millis();
            arbitration_address = d;

            return;
        }
    }
    if (c == CMD_SEND){
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

size_t arbitrateEnhClient(WiFiClient* client, EBusState& busstate, uint8_t* bytes){
    int bytesread = 0;
    if (client->availableForWrite() >= AVAILABLE_THRESHOLD) {
        // only allowed to start arbitration when the bus is in "eReceivedFirstSYN" state
        if (    busstate._state == EBusState::eReceivedFirstSYN && 
                Serial.available() == 0 && 
                arbitration_client == client) 
            { 
            // start of arbitration
            bool participateSecond = false;
            bool won = false;
            
            Serial.write(arbitration_address);
            while (busstate._state != EBusState::eBusy && !won ){
                if (bytesread == ARBITRATION_BUFFER_SIZE-1){
                        send_res(client, FAILED, 0x3f);
                        arbitration_client = NULL;
                        return bytesread;                    
                }
                while (Serial.available() == 0) {
                    if (millis() > arbitration_start + ARBITRATION_TIMEOUT_MS) {
                        send_res(client, FAILED, 0x3f);
                        arbitration_client = NULL;
                        return bytesread;
                    }
                }
                uint8_t symbol = Serial.read();
                busstate.data(symbol);
                bytes[bytesread++] = symbol;

                if (busstate._state == EBusState::eReceivedAddressAfterFirstSYN) {
                    if (symbol == arbitration_address) {
                        won = true; // we won; nobody else will write to the bus
                    } else if ((symbol & 0b11110000) == (arbitration_address & 0b11110000)) { 
                        participateSecond = true; // participate in second round of arbitration if we have the same priority class
                    }
                    else {
                        // arbitration might be ongoing between other bus participants, so we cannot yet know what 
                        // the winning master is. Need to wait for eBusy
                    }
                }
                if (busstate._state == EBusState::eReceivedSecondSYN && participateSecond) {
                    // participate in second round of arbitration
                    Serial.write(arbitration_address);
                }
                if (busstate._state == EBusState::eReceivedAddressAfterSecondSYN) {
                    if (symbol == arbitration_address) {
                        won = true; // we won; nobody else will write to the bus
                    }
                    else {
                        // we now know which address has won and we could exit here. 
                        // but it is easier to wait for eBusy, so after the while loop, the 
                        // "lost" state can be handled the same as when somebody lost in the first round
                    }
                }
            }
            if (won) {
                send_res(client, STARTED, busstate._master);
            }
            else {
                // Report FAILED arbitration. Include the extra byte.
                send_res(client, FAILED, busstate._master);
                send_received(client, busstate._byte);  
            }   
            arbitration_client = NULL;     
        }
        if (arbitration_client && (millis() > arbitration_start + ARBITRATION_TIMEOUT_MS)){
            send_res(arbitration_client, FAILED, 0x3f);
            arbitration_client = NULL;
        }
    }
    return bytesread;
}

int pushEnhClient(WiFiClient* client, uint8_t B){
    if (client->availableForWrite() >= AVAILABLE_THRESHOLD) {
        send_received(client, B);
        return 1;
    }
    return 0;
}
