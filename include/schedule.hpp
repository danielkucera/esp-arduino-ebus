#ifndef _SCHEDULE_H_
#define _SCHEDULE_H_

#include <WiFiClient.h>
#include "Datatypes.h"

// Implementation of the perodic sending of predefined commands.
// The results are provided as a json string.

struct Command
{
    const char *data;      // ebus command as ZZ PB SB NN DBx
    const char *desc;      // description
    const char *unit;      // unit of interested value
    const ebus::type type; // datatype of interested value
    size_t pos;            // position of interested value
    uint32_t uvalue;       // value as unsigned char/integer
    int32_t ivalue;        // value as char/interger
    double dvalue;         // value as double
};

void setPublishCallback(std::function<void(const char *topic, const char *payload)> func);

size_t getCommands();
size_t getCommandIndex();
unsigned long getCommandCounter();

std::string printCommandDescription(size_t index);
std::string printCommandValue(size_t index);

void processSend();
bool processReceive(bool enhanced, WiFiClient *client, const uint8_t byte);

String printCommandJsonData();
String printCommandJsonData(size_t i);

bool busReadyCallback();
void busWriteCallback(const uint8_t byte);
void saveResponseCallback(const std::vector<uint8_t> response);

#endif