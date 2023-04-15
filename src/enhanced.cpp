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
            send_res(client, FAILED, 0x3f);
            return;
        } else {
            // start arbitration
            if (arbitration_client ) {
                if (arbitration_client!=client) {
                    // only one client can be in arbitration
                    DEBUG_LOG("CMD_START ONGOING 0x%02 0x%02x\n", arbitration_address, d);
                    send_res(client, FAILED, 0x3f);
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

size_t arbitrateEnhClient(WiFiClient* client, EBusState& busstate, uint8_t* bytes){
    size_t bytesread = 0;
    static int arb = 0;
    if (client->availableForWrite() >= AVAILABLE_THRESHOLD) {
        // only allowed to start arbitration when the bus is in "eReceivedFirstSYN" state
        if (    busstate._state == EBusState::eReceivedFirstSYN && 
                arbitration_client == client) 
            { 
            
            // Arbitration is timing sensitive. Avoid communicating with WifiClient during arbitration
            DEBUG_LOG("ARB START %04i 0x%02x %ld us\n", arb++, arbitration_address, busstate.passedsincesyn());
            
            if (Serial.available() != 0)
            {
                send_res(client, RECEIVED, SYN); // ebusd expects the starting SYN
                send_res(client, FAILED, 0x3f);
                DEBUG_LOG("ARB LATE 0x%02x\n", Serial.peek());
                arbitration_client=NULL;
                return 0;
            }

            // start of arbitration
            bool participateSecond = false;
            bool won = false;
            int  loopcount = 0;
            
            DEBUG_LOG("ARB MASTER1    0x%02x %ld us\n", arbitration_address, busstate.passedsincesyn());
            Serial.write(arbitration_address);

            while (busstate._state != EBusState::eBusy && !won && loopcount++ < ARBITRATION_BUFFER_SIZE){
                while (Serial.available() == 0) {
                    if (millis() > arbitration_start + ARBITRATION_TIMEOUT_MS) {
                        DEBUG_LOG("ARB TIMEOUT 1 0x%02x 0x%02x\n", busstate._master, busstate._byte);
                        send_res(client, RECEIVED, SYN); // ebusd expects the starting SYN
                        send_res(client, FAILED, 0x3f);
                        arbitration_client = NULL;
                        return bytesread;
                    }
                }
                uint8_t symbol = Serial.read();
                DEBUG_LOG("ARB SYMBOL     0x%02x %ld us\n", symbol, busstate.passedsincesyn());
                busstate.data(symbol);
                bytes[bytesread++] = symbol;

                switch (busstate._state) 
                {
                case EBusState::eStartup: // error out
                    DEBUG_LOG("ARB STARTUP    0x%02x 0x%02x\n", busstate._master, busstate._byte);
                    send_res(client, RECEIVED, SYN); // ebusd expects the starting SYN
                    send_res(client, FAILED, 0x3f);
                    arbitration_client = NULL;
                    return bytesread;
                case EBusState::eReceivedAddressAfterFirstSYN: // did we win 1st round of abitration?
                    if (symbol == arbitration_address) {
                        DEBUG_LOG("ARB WON1       0x%02x %ld us\n", symbol, busstate.passedsincesyn());
                        won = true; // we won; nobody else will write to the bus
                    } else if ((symbol & 0b00001111) == (arbitration_address & 0b00001111)) { 
                        DEBUG_LOG("ARB PART SECND 0x%02x 0x%02x\n", arbitration_address, symbol);
                        participateSecond = true; // participate in second round of arbitration if we have the same priority class
                    }
                    else {
                        DEBUG_LOG("ARB LOST1      0x%02x %ld us\n", symbol, busstate.passedsincesyn());
                        // arbitration might be ongoing between other bus participants, so we cannot yet know what 
                        // the winning master is. Need to wait for eBusy
                    }
                    break;
                case EBusState::eReceivedSecondSYN: // did we sign up for second round arbitration?
                    if (participateSecond) {
                        // execute second round of arbitration
                        DEBUG_LOG("ARB MASTER2    0x%02x %ld us\n", arbitration_address, busstate.passedsincesyn());
                        Serial.write(arbitration_address);
                    }
                    break;
                case EBusState::eReceivedAddressAfterSecondSYN: // did we win 2nd round of arbitration?
                    if (symbol == arbitration_address) {
                        DEBUG_LOG("ARB WON2       0x%02x %ld us\n", symbol, busstate.passedsincesyn());
                        won = true; // we won; nobody else will write to the bus
                    }
                    else {
                        DEBUG_LOG("ARB LOST2      0x%02x %ld us\n", symbol, busstate.passedsincesyn());
                        // we now know which address has won and we could exit here. 
                        // but it is easier to wait for eBusy, so after the while loop, the 
                        // "lost" state can be handled the same as when somebody lost in the first round
                    }
                    break;
                }
            }
            if (won) {
                DEBUG_LOG("ARB SEND WON   0x%02x %ld us\n", busstate._master, busstate.passedsincesyn());
                send_res(client, RECEIVED, SYN); // ebusd expects the starting SYN
                send_res(client, STARTED, busstate._master);

            }
            else {
                DEBUG_LOG("ARB SEND LOST  0x%02x 0x%02x %ld us\n", busstate._master, busstate._byte, busstate.passedsincesyn());
                send_res(client, RECEIVED, SYN); // ebusd expects the starting SYN
                send_res(client, FAILED, busstate._master);
                send_res(client, RECEIVED, busstate._byte);
            }   
            arbitration_client = NULL;     
        }
        if (arbitration_client && (millis() > arbitration_start + ARBITRATION_TIMEOUT_MS)){
            DEBUG_LOG("ARB TIMEOUT 2 0x%02x 0x%02x\n", busstate._master, busstate._byte);
            send_res(client, RECEIVED, SYN); // ebusd expects the starting SYN
            send_res(arbitration_client, FAILED, 0x3f);
            arbitration_client = NULL;
        }
    }
    return bytesread;
}

int pushEnhClient(WiFiClient* client, uint8_t B){
    if (client->availableForWrite() >= AVAILABLE_THRESHOLD) {
        send_res(client, RECEIVED,  B);
        return 1;
    }
    return 0;
}
