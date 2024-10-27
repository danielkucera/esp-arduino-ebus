#ifndef _SCHEDULE_H_
#define _SCHEDULE_H_

#include <WiFiClient.h>

#include "EbusHandler.h"
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

class Schedule
{

public:
    Schedule();

    void setAddress(const uint8_t source);    
    void setPublishCallback(std::function<void(const char *topic, const char *payload)> publishFunction);

    bool needTX();

    void processSend();
    bool processReceive(bool enhanced, WiFiClient *client, const uint8_t byte);

private:
    uint8_t address = 0xff; // TODO 0xff Systemparameter ?
    std::function<void(const char *topic, const char *payload)> publishCallback = nullptr;

    WiFiClient *dummyClient = new WiFiClient();
    ebus::EbusHandler ebusHandler;

    Command *actCommand = nullptr;

    unsigned long distanceCommands = 5 * 1000; // TODO Systemparameter ?
    unsigned long lastCommand = 0;

    bool initDone = false;

    const std::vector<uint8_t> nextCommand();

    static bool busReadyCallback();
    static void busWriteCallback(const uint8_t byte);
    static void responseCallback(const std::vector<uint8_t> response);

    void processResponse(const std::vector<uint8_t> vec);
};

extern Schedule schedule;

#endif