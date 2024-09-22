#ifndef _SCHEDULE_H_
#define _SCHEDULE_H_

#include <WiFiClient.h>

// Implementation of the perodic sending of predefined commands (Master-Slave Telegram)
// based on the ebus classes Telegram, Sequence and Datatypes.
// The results are provided as a json string.

enum class datatype
{
    bcd, // BCD
    uch, // uint8_t
    sch, // int8_t
    uin, // uint16_t
    sin, // int16_t
    ulg, // uint32_t
    slg, // int32_t
    d1b, // DATA1b
    d1c, // DATA1c
    d2b, // DATA2b
    d2c, // DATA2c
    flt  // float
};

struct Command
{
    const char *data;    // ebus command as ZZ PB SB NN DBx
    const char *desc;    // description
    const char *unit;    // unit of interested value
    const datatype type; // datatype of interested value
    int pos;             // position of interested value
    uint32_t uvalue;     // value as unsigned char/integer
    int32_t ivalue;      // value as char/interger
    double dvalue;       // value as double
};

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

#endif