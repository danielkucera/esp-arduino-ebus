#ifndef _SCHEDULE_H_
#define _SCHEDULE_H_

#include <WiFiClient.h>

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

std::string printCommandDescription(size_t index);
std::string printCommandValue(size_t index);

bool handleSchedule();
bool pushSchedule(bool enhanced, WiFiClient *client, const uint8_t byte);

size_t printCommandState();
unsigned long printCommandCounter();
size_t printCommandIndex();
String printCommandMaster();
size_t printCommandMasterSize();
size_t printCommandMasterSendIndex();
size_t printCommandMasterRecvIndex();
size_t printCommandMasterState();
String printCommandSlave();
size_t printCommandSlaveSize();
size_t printCommandSlaveIndex();
size_t printCommandSlaveState();

String printCommandJsonData();

#endif