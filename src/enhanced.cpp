#include <WiFiClient.h>
#include "main.hpp"

#define M1 0b11000000
#define M2 0b10000000

#define ARBITRATION_TIMEOUT_MS 200

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

    client->write(dat[0]);
    client->write(dat[1]);

}

uint8_t* read_cmd(WiFiClient* client){
    int b, b2;

    b = client->read();

    if (b<0){
        // available and read -1 ???
        return NULL;
    }

    if (b<0b10000000){
        Serial.write(b);
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

void process_cmd(WiFiClient* client, uint8_t c, uint8_t d){
    if (c == CMD_INIT){
        send_res(client, RESETTED, 0x0);
        return;
    }
    if (c == CMD_START){
        int qq_sent = 0;

        if (d == SYN){
            // abort arbitration... ?
            return;
        } else {
            // start arbitration
            unsigned long start = millis();

            while (millis() < start + ARBITRATION_TIMEOUT_MS){
                if (client->available()){
                    if (uint8_t* cmd = read_cmd(client)){
                        if ((cmd[0] == CMD_START) && (cmd[1] == SYN)){
                            // abort arbitration, TODO: response
                            return;
                        } else {
                            // TODO: should we process client data during arbitration?
                            //process_cmd(client, cmd[0], cmd[1]);
                        }
                    }
                }

                if (Serial.available()) {
                    int s = Serial.read();

                    if (Serial.available()){ // is there still data in buffer after reading?
                        continue;
                    }

                    if (s >= 0) {
                        //pushEnhClient(client, s);
                        if (qq_sent){
                            if (s == d){
                                // arbitration success
                                send_res(client, STARTED, d);
                                return;
                            } else {
                                // arbitration fail: QQ sent, received other
                                //send_res(client, FAILED, s);
                                //return;
                                qq_sent = 0;
                            }
                        }
                        if (s == SYN) {
                            //delay(d*10); //TODO: verify wait master address * 10

                            Serial.write(d);
                            qq_sent = 1;
                        }
                    }
                }
            }
            // arbitration timeout
            send_res(client, FAILED, 0x3f);
            return;
        }
    }
    if (c == CMD_SEND){
        Serial.write(d);
        return;
    }
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

        if (B < 0x80){
            client->write(B);
        } else {
            send_res(client, RECEIVED, B);
        }
        return 1;
    }
    return 0;
}