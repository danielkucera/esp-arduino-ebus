#ifndef _SCHEDULE_H_
#define _SCHEDULE_H_

#include <WiFiClient.h>
#include "Datatypes.h"

// Implementation of the perodic sending of predefined commands (Master-Slave Telegram)
// based on the ebus classes Telegram, Sequence and Datatypes.
// The results are provided as a json string.

struct Command
{
    const char *data;      // ebus command as ZZ PB SB NN DBx
    const char *desc;      // description
    const char *unit;      // unit of interested value
    const ebus::type type; // datatype of interested value
    int pos;               // position of interested value
    uint32_t uvalue;       // value as unsigned char/integer
    int32_t ivalue;        // value as char/interger
    double dvalue;         // value as double
};

void setPublichCallback(std::function<void(const char *topic, const char *payload)> func);

size_t getCommands();
size_t getCommandIndex();
unsigned long getCommandCounter();

std::string printCommandDescription(size_t index);
std::string printCommandValue(size_t index);

void handleScheduleSend();
bool handleScheduleRecv(bool enhanced, WiFiClient *client, const uint8_t byte);

String printCommandMaster();
size_t printCommandMasterState();
String printCommandSlave();
size_t printCommandSlaveState();

String printCommandJsonData();
String printCommandJsonData(size_t i);

#endif