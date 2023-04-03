#include <WiFiClient.h>
#include "main.hpp"

#define M1 0b11000000
#define M2 0b10000000

#define ARBITRATION_TIMEOUT_MS 2000

enum symbols {
    SYN = 0xAA
};

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

uint8_t* decode(int b1, int b2){
    static uint8_t ret[2];

    ret[0] = (b1 >> 2) & 0b1111;
    ret[1] = ((b1 & 0b11) << 6) | (b2 & 0b00111111);

    return ret;
}

uint8_t* encode(uint8_t c, uint8_t d){
    static uint8_t ret[2];

    ret[0] = M1 | c << 2 | d >> 6;
    ret[1] = M2 | (d & 0b00111111);

    return ret;
}

void send_res(WiFiClient* client, uint8_t c, uint8_t d){
    uint8_t* dat = encode(c, d);

    client->write(dat, 2);

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
}

uint8_t* read_cmd(WiFiClient* client){
    int b, b2;

    b = client->read();

    if (b<0){
        // available and read -1 ???
        return NULL;
    }

    if (b<0b10000000){
        process_cmd(client, CMD_SEND, b);
        return NULL;
    }

    if (b<0b11000000){
        client->write("first command signature error");
        // first command signature error
        client->stop();
        return NULL;
    }

    b2 = client->read();

    if (b2<0) {
        // second command missing
        client->write("second command missing");
        client->stop();
        return NULL;
    }

    if ((b2 & 0b11000000) != 0b10000000){
        // second command signature error
        client->write("second command signature error");
        client->stop();
        return NULL;
    }

    return decode(b, b2);
}

void handleEnhClient(WiFiClient* client){

    while (client->available()) {
        uint8_t* dat = read_cmd(client);

        if (dat) {
            process_cmd(client, dat[0], dat[1]);
        }
    }

}

int pushEnhClient(WiFiClient* client, uint8_t B){
    if (client->availableForWrite() >= AVAILABLE_THRESHOLD) {

        if ((B == SYN) && (Serial.available() == 0)){
            if (arbitration_client == client) {
                Serial.write(arbitration_address);

                while (Serial.available() == 0) {
                    if (millis() > arbitration_start + ARBITRATION_TIMEOUT_MS) {
                        send_res(client, FAILED, 0x3f);
                        arbitration_client = NULL;
                        return 1;
                    }
                }

                if (Serial.read() == arbitration_address){ // arbitration success
                    send_res(client, STARTED, arbitration_address);
                    arbitration_client = NULL;
                } else { // arbitration fail
                    // do nothing, arbitration will retry on next SYN until timeout or cancel
                }
                return 1;
            }
        }

        if (arbitration_client && (millis() > arbitration_start + ARBITRATION_TIMEOUT_MS)){
            send_res(arbitration_client, FAILED, 0x3f);
            arbitration_client = NULL;
        }

        if (B < 0x80){
            client->write(B);
        } else {
            send_res(client, RECEIVED, B);
        }
        return 1;
    }
    return 0;
}