#ifndef _SCHEDULE_H_
#define _SCHEDULE_H_

#include <WiFiClient.h>
#include "Datatypes.h"

// Implementation of the perodic sending of predefined commands.
// The results are provided as a json string.

struct Command
{
    const char *command;          // ebus command as ZZ PB SB NN DBx
    const char *unit;             // unit of the received data
    const unsigned long interval; // minimum interval between two commands in seconds
    unsigned long last;           // last time of the successful command
    const size_t position;        // starting position of the value in the response
    const ebus::type datatype;    // ebus datatype
    const char *topic;            // mqtt topic
};

bool needTX();
void setPublishCallback(std::function<void(const char *topic, const char *payload)> publishFunction);

void processSend();
bool processReceive(bool enhanced, WiFiClient *client, const uint8_t byte);

bool busReadyCallback();
void busWriteCallback(const uint8_t byte);
void responseCallback(const std::vector<uint8_t> response);

#endif