#include "schedule.hpp"

#include "EbusHandler.h"

#include "bus.hpp"

#include <sstream>
#include <iomanip>

// #include "command.hpp"
#ifndef _COMMAND_H_
std::vector<Command> commandTable;
#endif

// create include/command.hpp like this

// #ifndef _COMMAND_H_
// #define _COMMAND_H_

// #include "schedule.hpp"

// std::vector<Command> commandTable = {
// {specific commands}
// };

// #endif

size_t commandIndex = commandTable.size();
unsigned long commandCounter = 0;

unsigned long millisLastCommand = 0;
unsigned long distanceCommands = 10 * 1000; // TODO Systemparameter ?

WiFiClient *dummyClient = new WiFiClient();
ebus::EbusHandler ebusHandler(0xff, &busReadyCallback, &busWriteCallback, &saveResponseCallback); // TODO 0xff Systemparameter ?

std::function<void(const char *topic, const char *payload)> publishCallback = nullptr;

void setPublishCallback(std::function<void(const char *topic, const char *payload)> func)
{
    publishCallback = func;
}

void saveCommandValue(const std::vector<uint8_t> vec)
{
    size_t pos = commandTable[commandIndex].pos;

    switch (commandTable[commandIndex].type)
    {
    case ebus::type::BCD:
        commandTable[commandIndex].uvalue = ebus::byte_2_bcd(ebus::Sequence::range(vec, pos, 1));
        break;
    case ebus::type::UINT8:
        commandTable[commandIndex].uvalue = ebus::byte_2_uint8(ebus::Sequence::range(vec, pos, 1));
        break;
    case ebus::type::INT8:
        commandTable[commandIndex].ivalue = ebus::byte_2_int8(ebus::Sequence::range(vec, pos, 1));
        break;
    case ebus::type::UINT16:
        commandTable[commandIndex].uvalue = ebus::byte_2_uint16(ebus::Sequence::range(vec, pos, 2));
        break;
    case ebus::type::INT16:
        commandTable[commandIndex].ivalue = ebus::byte_2_int16(ebus::Sequence::range(vec, pos, 2));
        break;
    case ebus::type::UINT32:
        commandTable[commandIndex].uvalue = ebus::byte_2_uint32(ebus::Sequence::range(vec, pos, 4));
        break;
    case ebus::type::INT32:
        commandTable[commandIndex].ivalue = ebus::byte_2_int32(ebus::Sequence::range(vec, pos, 4));
        break;
    case ebus::type::DATA1b:
        commandTable[commandIndex].dvalue = ebus::byte_2_data1b(ebus::Sequence::range(vec, pos, 1));
        break;
    case ebus::type::DATA1c:
        commandTable[commandIndex].dvalue = ebus::byte_2_data1c(ebus::Sequence::range(vec, pos, 1));
        break;
    case ebus::type::DATA2b:
        commandTable[commandIndex].dvalue = ebus::byte_2_data2b(ebus::Sequence::range(vec, pos, 2));
        break;
    case ebus::type::DATA2c:
        commandTable[commandIndex].dvalue = ebus::byte_2_data2c(ebus::Sequence::range(vec, pos, 2));
        break;
    case ebus::type::FLOAT:
        commandTable[commandIndex].dvalue = ebus::byte_2_float(ebus::Sequence::range(vec, pos, 2));
        break;
    default:
        break;
    }

    if (publishCallback != nullptr)
    {
        String topic = "ebus/data/";
        topic += commandTable[commandIndex].desc;
        publishCallback(topic.c_str(), printCommandJsonData(commandIndex).c_str());
    }
}

size_t getCommands()
{
    return commandTable.size();
}

size_t getCommandIndex()
{
    return commandIndex;
}

unsigned long getCommandCounter()
{
    return commandCounter;
}

std::string printCommandDescription(size_t index)
{
    std::ostringstream ostr;
    ostr << index << "_" << commandTable[index].desc << "_" << commandTable[index].unit;
    return ostr.str();
}

std::string printCommandValue(size_t index)
{
    std::ostringstream ostr;

    switch (commandTable[index].type)
    {
    case ebus::type::BCD:
    case ebus::type::UINT8:
    case ebus::type::UINT16:
    case ebus::type::UINT32:
        ostr << commandTable[index].uvalue;
        break;
    case ebus::type::INT8:
    case ebus::type::INT16:
    case ebus::type::INT32:
        ostr << commandTable[index].ivalue;
        break;
    case ebus::type::DATA1b:
    case ebus::type::DATA1c:
    case ebus::type::DATA2b:
    case ebus::type::DATA2c:
    case ebus::type::FLOAT:
        ostr << commandTable[index].dvalue;
        break;
    default:
        break;
    }

    return ostr.str();
}

std::string escape_json(const std::string &str)
{
    std::ostringstream ostr;
    for (auto chr = str.cbegin(); chr != str.cend(); chr++)
    {
        if (*chr == '"' || *chr == '\\' || ('\x00' <= *chr && *chr <= '\x1f'))
            ostr << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(*chr);
        else
            ostr << *chr;
    }
    return ostr.str();
}

Command nextCommand()
{
    commandCounter++;
    commandIndex++;

    if (commandIndex >= commandTable.size())
        commandIndex = 0;

    return commandTable[commandIndex];
}

void processSend()
{
    if (commandTable.size() == 0)
        return;

    if (ebusHandler.getState() == ebus::State::MonitorBus)
    {
        if (millis() > millisLastCommand + distanceCommands)
        {
            millisLastCommand = millis();

            if (ebusHandler.enque(ebus::Sequence::to_vector(nextCommand().data)))
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
    if (commandTable.size() == 0)
        return false;

    if (ebusHandler.getState() == ebus::State::Arbitration)
    {
        // workaround - master sequence comes twice - waiting for second master sequence without "enhanced" flag
        if (!enhanced && client == dummyClient)
            ebusHandler.receive(byte);

        // reset - if arbitration went wrong
        if (millis() > millisLastCommand + (distanceCommands / 4))
            ebusHandler.reset();
    }
    else
    {
        ebusHandler.receive(byte);
    }

    return true;
}

String printCommandJsonData()
{
    String s = "{\"esp-eBus\":{\"Data\":{";
    for (size_t i = 0; i < commandTable.size(); i++)
    {
        s += "\"" + String(escape_json(printCommandDescription(i)).c_str()) + "\":";
        s += String(escape_json(printCommandValue(i)).c_str()) + ",";
    }

    if (commandTable.size() > 0)
        s.remove(s.length() - 1, 1);

    s += "}}}";

    return s;
}

String printCommandJsonData(size_t i)
{
    String s = "{\"esp-eBus\":{\"Data\":{";
    s += "\"" + String(escape_json(printCommandDescription(i)).c_str()) + "\":";
    s += String(escape_json(printCommandValue(i)).c_str()) + "}}}";
    return s;
}

bool busReadyCallback()
{
    return Bus.availableForWrite();
}

void busWriteCallback(const uint8_t byte)
{
    Bus.write(byte);
}

void saveResponseCallback(const std::vector<uint8_t> response)
{
    saveCommandValue(response);
}
