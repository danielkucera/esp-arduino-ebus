#ifndef _SCHEDULE_H_
#define _SCHEDULE_H_

#include <WiFiClient.h>

#include "EbusHandler.h"
#include "Statistics.h"
#include "Datatypes.h"

// Implementation of the perodic sending of predefined commands.

struct Command
{
    std::string command;     // ebus command as ZZ PB SB NN DBx
    std::string unit;        // unit of the received data
    unsigned long interval;  // minimum interval between two commands in seconds
    unsigned long last;      // last time of the successful command
    size_t position;         // starting position of the value in the response
    ebus::Datatype datatype; // ebus datatype
    std::string topic;       // mqtt topic
};

class Schedule
{

public:
    Schedule();

    void setAddress(const uint8_t source);

    void insertCommand(const char *payload);
    void removeCommand(const char *payload);
    void publishCommands() const;

    bool needTX();

    void processSend();
    bool processReceive(bool enhanced, WiFiClient *client, const uint8_t byte);

    void resetStatistics();
    void publishCounters();

private:
    uint8_t address = 0xff; // TODO 0xff Systemparameter ?

    std::map<std::string, Command> commands;

    WiFiClient *dummyClient = new WiFiClient();
    ebus::EbusHandler ebusHandler;
    ebus::Statistics statistics;

    bool initCounters = true;
    ebus::Counter lastCounters;

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