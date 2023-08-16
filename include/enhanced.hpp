#ifndef _ENHANCED_H_
#define _ENHANCED_H_
#include <WiFiClient.h>

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

enum errors {
    ERR_FRAMING = 0x00,
    ERR_OVERRUN = 0x01
};

void        enhArbitrationDone();
WiFiClient* enhArbitrationRequested(uint8_t& arbitration_client);

int    pushEnhClient(WiFiClient* client, uint8_t c, uint8_t d, bool log);
void   handleEnhClient(WiFiClient* client);

#endif
