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
    bool ha;                 // home assistant support for auto discovery
    std::string ha_class;    // home assistant device_class
};

class Schedule
{

public:
    Schedule();

    void setAddress(const uint8_t source);
    void setDistance(const uint8_t distance);

    void insertCommand(const char *payload);
    void removeCommand(const char *topic);

    void publishCommands() const;

    bool needTX();

    void processSend();
    bool processReceive(bool enhanced, WiFiClient *client, const uint8_t byte);

    void resetStatistics();
    void publishCounters();

private:
    uint8_t address = 0xff;

    std::map<std::string, Command> commands;

    WiFiClient *dummyClient = new WiFiClient();
    ebus::EbusHandler ebusHandler;
    ebus::Statistics statistics;

    bool initCounters = true;
    ebus::Counter lastCounters;

    Command *actCommand = nullptr;

    unsigned long distanceCommands = 0;
    unsigned long lastCommand = 0;

    bool initDone = false;

    void publishCommand(const char *key, bool remove) const;
    void publishHomeAssistant(const char *key, bool remove) const;

    const std::vector<uint8_t> nextCommand();

    static bool busReadyCallback();
    static void busWriteCallback(const uint8_t byte);
    static void responseCallback(const std::vector<uint8_t> response);

    void processResponse(const std::vector<uint8_t> vec);
};

extern Schedule schedule;

#endif