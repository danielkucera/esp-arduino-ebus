#include "schedule.hpp"

#include "EbusHandler.h"

#include "bus.hpp"

#include <sstream>
#include <iomanip>

// #include "command.hpp"
#ifndef _COMMAND_H_
std::vector<Command> commands;
#endif

// create include/command.hpp like this

// #ifndef _COMMAND_H_
// #define _COMMAND_H_

// #include "schedule.hpp"

// std::vector<Command> commands = {
// {specific commands}
// };

// #endif

size_t idx = commands.size();
unsigned long counter = 0;

unsigned long lastCommand = 0;
unsigned long distanceCommands = 10 * 1000; // TODO Systemparameter ?

WiFiClient *dummyClient = new WiFiClient();
ebus::EbusHandler ebusHandler(0xff, &busReadyCallback, &busWriteCallback, &responseCallback); // TODO 0xff Systemparameter ?

std::function<void(const char *topic, const char *payload)> publishCallback = nullptr;

size_t getCommands()
{
    return commands.size();
}

void setPublishCallback(std::function<void(const char *topic, const char *payload)> function)
{
    publishCallback = function;
}

const char *escape_json(const std::string &str)
{
    std::ostringstream ostr;
    for (auto chr = str.cbegin(); chr != str.cend(); chr++)
    {
        if (*chr == '"' || *chr == '\\' || ('\x00' <= *chr && *chr <= '\x1f'))
            ostr << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(*chr);
        else
            ostr << *chr;
    }
    return ostr.str().c_str();
}

void processResponse(const std::vector<uint8_t> vec)
{
    commands[idx].last = millis();

    if (publishCallback != nullptr)
    {
        size_t pos = commands[idx].position;
        std::ostringstream ostr;

        switch (commands[idx].datatype)
        {
        case ebus::type::BCD:
            ostr << ebus::byte_2_bcd(ebus::Sequence::range(vec, pos, 1));
            break;
        case ebus::type::UINT8:
            ostr << ebus::byte_2_uint8(ebus::Sequence::range(vec, pos, 1));
            break;
        case ebus::type::INT8:
            ostr << ebus::byte_2_int8(ebus::Sequence::range(vec, pos, 1));
            break;
        case ebus::type::UINT16:
            ostr << ebus::byte_2_uint16(ebus::Sequence::range(vec, pos, 2));
            break;
        case ebus::type::INT16:
            ostr << ebus::byte_2_int16(ebus::Sequence::range(vec, pos, 2));
            break;
        case ebus::type::UINT32:
            ostr << ebus::byte_2_uint32(ebus::Sequence::range(vec, pos, 4));
            break;
        case ebus::type::INT32:
            ostr << ebus::byte_2_int32(ebus::Sequence::range(vec, pos, 4));
            break;
        case ebus::type::DATA1b:
            ostr << ebus::byte_2_data1b(ebus::Sequence::range(vec, pos, 1));
            break;
        case ebus::type::DATA1c:
            ostr << ebus::byte_2_data1c(ebus::Sequence::range(vec, pos, 1));
            break;
        case ebus::type::DATA2b:
            ostr << ebus::byte_2_data2b(ebus::Sequence::range(vec, pos, 2));
            break;
        case ebus::type::DATA2c:
            ostr << ebus::byte_2_data2c(ebus::Sequence::range(vec, pos, 2));
            break;
        case ebus::type::FLOAT:
            ostr << ebus::byte_2_float(ebus::Sequence::range(vec, pos, 2));
            break;
        default:
            break;
        }

        String payload = "{\"value\":" + String(ostr.str().c_str()) + ",";
        payload += "\"unit\":\"" + String(escape_json(commands[idx].unit)) + "\",";
        payload += "\"command\":\"" + String(commands[idx].command) + "\",";
        payload += "\"interval\":" + String(commands[idx].interval) + "}";

        publishCallback(escape_json(commands[idx].topic), payload.c_str());
    }
}

Command nextCommand()
{
    counter++;
    
    idx++;

    if (idx >= commands.size())
        idx = 0;

    return commands[idx];
}

void processSend()
{
    if (commands.size() == 0)
        return;

    if (ebusHandler.getState() == ebus::State::MonitorBus)
    {
        if (millis() > lastCommand + distanceCommands)
        {
            lastCommand = millis();

            if (ebusHandler.enque(ebus::Sequence::to_vector(nextCommand().command)))
            {
                // start arbitration
                WiFiClient *client = dummyClient;
                uint8_t address = ebusHandler.getAddress();

                if (!setArbitrationClient(client, address))
                {
                    if (client != dummyClient)
                    {
                        DEBUG_LOG("ARBITRATION ONGOING 0x%02 0x%02x\n", address, ebusHandler.getAddress());
                    }
                }
                else
                {
                    DEBUG_LOG("ARBITRATION START 0x%02x\n", address);
                }
            }
        }
    }

    ebusHandler.send();
}

bool processReceive(bool enhanced, WiFiClient *client, const uint8_t byte)
{
    if (commands.size() == 0)
        return false;

    if (ebusHandler.getState() == ebus::State::Arbitration)
    {
        // workaround - master sequence comes twice - waiting for second master sequence without "enhanced" flag
        if (!enhanced && client == dummyClient)
            ebusHandler.receive(byte);

        // reset - if arbitration went wrong
        if (millis() > lastCommand + (distanceCommands / 4))
            ebusHandler.reset();
    }
    else
    {
        ebusHandler.receive(byte);
    }

    return true;
}

bool busReadyCallback()
{
    return Bus.availableForWrite();
}

void busWriteCallback(const uint8_t byte)
{
    Bus.write(byte);
}

void responseCallback(const std::vector<uint8_t> response)
{
    processResponse(response);
}
