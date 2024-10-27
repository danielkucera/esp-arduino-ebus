#include "schedule.hpp"

#include "bus.hpp"

#include <ArduinoJson.h>

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

Schedule schedule;

Schedule::Schedule()
{
    ebusHandler = ebus::EbusHandler(address, &busReadyCallback, &busWriteCallback, &responseCallback);
}

void Schedule::setAddress(const uint8_t source)
{
    address = source;
    ebusHandler.setAddress(source);
}

void Schedule::setPublishCallback(std::function<void(const char *topic, const char *payload)> publishFunction)
{
    publishCallback = publishFunction;
}

bool Schedule::needTX()
{
    return commands.size() > 0;
}

void Schedule::processSend()
{
    if (commands.size() == 0)
        return;

    if (ebusHandler.getState() == ebus::State::MonitorBus)
    {
        if (millis() > lastCommand + distanceCommands)
        {
            lastCommand = millis();

            if (ebusHandler.enque(nextCommand()))
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

bool Schedule::processReceive(bool enhanced, WiFiClient *client, const uint8_t byte)
{
    // TODO statistic

    if (commands.size() == 0)
        return false;

    if (ebusHandler.getState() == ebus::State::Arbitration)
    {
        // workaround - master sequence comes twice - waiting for second master sequence without "enhanced" flag
        if (!enhanced && client == dummyClient)
            ebusHandler.receive(byte);

        // reset - if arbitration went wrong
        if (millis() > lastCommand + 1 * 1000)
            ebusHandler.reset();
    }
    else
    {
        ebusHandler.receive(byte);
    }

    return true;
}

const std::vector<uint8_t> Schedule::nextCommand()
{
    if (!initDone)
    {
        size_t count = std::count_if(commands.begin(), commands.end(),
                                     [](Command cmd)
                                     { return cmd.last == 0; });
        if (count == 0)
        {
            initDone = true;
        }
        else
        {
            for (std::vector<Command>::iterator it = commands.begin(); it != commands.end(); ++it)
            {
                if (it->last == 0)
                {
                    actCommand = &(*it);
                    break;
                }
            }
        }
    }
    else
    {
        actCommand = &(*std::min_element(commands.begin(), commands.end(),
                                         [](const Command &i, const Command &j)
                                         { return (i.last + i.interval * 1000) < (j.last + j.interval * 1000); }));
    }

    return ebus::Sequence::to_vector(actCommand->command);
}

bool Schedule::busReadyCallback()
{
    return Bus.availableForWrite();
}

void Schedule::busWriteCallback(const uint8_t byte)
{
    Bus.write(byte);
}

void Schedule::responseCallback(const std::vector<uint8_t> response)
{
    schedule.processResponse(std::vector<uint8_t>(response));
}

void Schedule::processResponse(const std::vector<uint8_t> vec)
{
    actCommand->last = millis();

    if (publishCallback != nullptr)
    {
        JsonDocument doc;

        switch (actCommand->datatype)
        {
        case ebus::type::BCD:
            doc["value"] = ebus::byte_2_bcd(ebus::Sequence::range(vec, actCommand->position, 1));
            break;
        case ebus::type::UINT8:
            doc["value"] = ebus::byte_2_uint8(ebus::Sequence::range(vec, actCommand->position, 1));
            break;
        case ebus::type::INT8:
            doc["value"] = ebus::byte_2_int8(ebus::Sequence::range(vec, actCommand->position, 1));
            break;
        case ebus::type::UINT16:
            doc["value"] = ebus::byte_2_uint16(ebus::Sequence::range(vec, actCommand->position, 2));
            break;
        case ebus::type::INT16:
            doc["value"] = ebus::byte_2_int16(ebus::Sequence::range(vec, actCommand->position, 2));
            break;
        case ebus::type::UINT32:
            doc["value"] = ebus::byte_2_uint32(ebus::Sequence::range(vec, actCommand->position, 4));
            break;
        case ebus::type::INT32:
            doc["value"] = ebus::byte_2_int32(ebus::Sequence::range(vec, actCommand->position, 4));
            break;
        case ebus::type::DATA1b:
            doc["value"] = ebus::byte_2_data1b(ebus::Sequence::range(vec, actCommand->position, 1));
            break;
        case ebus::type::DATA1c:
            doc["value"] = ebus::byte_2_data1c(ebus::Sequence::range(vec, actCommand->position, 1));
            break;
        case ebus::type::DATA2b:
            doc["value"] = ebus::byte_2_data2b(ebus::Sequence::range(vec, actCommand->position, 2));
            break;
        case ebus::type::DATA2c:
            doc["value"] = ebus::byte_2_data2c(ebus::Sequence::range(vec, actCommand->position, 2));
            break;
        case ebus::type::FLOAT:
            doc["value"] = ebus::byte_2_float(ebus::Sequence::range(vec, actCommand->position, 2));
            break;
        default:
            break;
        }

        doc["unit"] = actCommand->unit;
        doc["interval"] = actCommand->interval;
        doc["last"] = actCommand->last/1000;

        String payload;
        serializeJson(doc, payload);

        publishCallback(actCommand->topic, payload.c_str());
    }
}
