#ifndef _SCHEDULE_H_
#define _SCHEDULE_H_

#include <WiFiClient.h>
#include "Datatypes.h"

// Implementation of the perodic sending of predefined commands.
// The results are provided as a json string.

struct Command
{
    const char *command;          // ebus command as ZZ PB SB NN DBx
    const char *unit;             // unit of received data
    const unsigned long interval; // time between two commands in seconds
    unsigned long last;           // last time of success command
    const size_t position;        // start position of value in response
    const ebus::type datatype;    // ebus datatype
    const char *topic;            // mqtt topic
};

size_t getCommands();
void setPublishCallback(std::function<void(const char *topic, const char *payload)> function);

void processSend();
bool processReceive(bool enhanced, WiFiClient *client, const uint8_t byte);

bool busReadyCallback();
void busWriteCallback(const uint8_t byte);
void responseCallback(const std::vector<uint8_t> response);

#endif