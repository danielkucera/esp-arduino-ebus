#include "schedule.hpp"

#include "bus.hpp"
#include "mqtt.hpp"

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

template <typename T>
void publishMQTTTopic(bool init, const char *topic, T &oldValue, T &newValue)
{
    if (init || oldValue != newValue)
        mqttClient.publish(topic, 0, true, String(newValue).c_str());
}

Schedule::Schedule()
{
    ebusHandler = ebus::EbusHandler(address, &busReadyCallback, &busWriteCallback, &responseCallback);
}

void Schedule::setAddress(const uint8_t source)
{
    address = source;
    ebusHandler.setAddress(source);
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
    if (!enhanced)
        statistics.collect(byte);

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

void Schedule::resetStatistics()
{
    statistics.reset();
}

void Schedule::publishMQTT()
{
    ebus::Counter counters = statistics.getCounters();

    publishMQTTTopic(initCounters, "ebus/messages/total", lastCounters.total, counters.total);

    publishMQTTTopic(initCounters, "ebus/messages/success", lastCounters.success, counters.success);
    publishMQTTTopic(initCounters, "ebus/messages/success/percent", lastCounters.successPercent, counters.successPercent);
    publishMQTTTopic(initCounters, "ebus/messages/success/master_slave", lastCounters.successMS, counters.successMS);
    publishMQTTTopic(initCounters, "ebus/messages/success/master_master", lastCounters.successMM, counters.successMM);
    publishMQTTTopic(initCounters, "ebus/messages/success/broadcast", lastCounters.successBC, counters.successBC);

    publishMQTTTopic(initCounters, "ebus/messages/failure", lastCounters.failure, counters.failure);
    publishMQTTTopic(initCounters, "ebus/messages/failure/percent", lastCounters.failurePercent, counters.failurePercent);

    publishMQTTTopic(initCounters, "ebus/messages/failure/master/empty", lastCounters.failureMaster[SEQ_EMPTY], counters.failureMaster[SEQ_EMPTY]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/master/ok", lastCounters.failureMaster[SEQ_OK], counters.failureMaster[SEQ_OK]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/master/short", lastCounters.failureMaster[SEQ_ERR_SHORT], counters.failureMaster[SEQ_ERR_SHORT]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/master/long", lastCounters.failureMaster[SEQ_ERR_LONG], counters.failureMaster[SEQ_ERR_LONG]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/master/nn", lastCounters.failureMaster[SEQ_ERR_NN], counters.failureMaster[SEQ_ERR_NN]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/master/crc", lastCounters.failureMaster[SEQ_ERR_CRC], counters.failureMaster[SEQ_ERR_CRC]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/master/ack", lastCounters.failureMaster[SEQ_ERR_ACK], counters.failureMaster[SEQ_ERR_ACK]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/master/qq", lastCounters.failureMaster[SEQ_ERR_QQ], counters.failureMaster[SEQ_ERR_QQ]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/master/zz", lastCounters.failureMaster[SEQ_ERR_ZZ], counters.failureMaster[SEQ_ERR_ZZ]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/master/ack_miss", lastCounters.failureMaster[SEQ_ERR_ACK_MISS], counters.failureMaster[SEQ_ERR_ACK_MISS]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/master/invalid", lastCounters.failureMaster[SEQ_ERR_INVALID], counters.failureMaster[SEQ_ERR_INVALID]);

    publishMQTTTopic(initCounters, "ebus/messages/failure/slave/empty", lastCounters.failureSlave[SEQ_EMPTY], counters.failureSlave[SEQ_EMPTY]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/slave/ok", lastCounters.failureSlave[SEQ_OK], counters.failureSlave[SEQ_OK]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/slave/short", lastCounters.failureSlave[SEQ_ERR_SHORT], counters.failureSlave[SEQ_ERR_SHORT]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/slave/long", lastCounters.failureSlave[SEQ_ERR_LONG], counters.failureSlave[SEQ_ERR_LONG]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/slave/nn", lastCounters.failureSlave[SEQ_ERR_NN], counters.failureSlave[SEQ_ERR_NN]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/slave/crc", lastCounters.failureSlave[SEQ_ERR_CRC], counters.failureSlave[SEQ_ERR_CRC]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/slave/ack", lastCounters.failureSlave[SEQ_ERR_ACK], counters.failureSlave[SEQ_ERR_ACK]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/slave/qq", lastCounters.failureSlave[SEQ_ERR_QQ], counters.failureSlave[SEQ_ERR_QQ]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/slave/zz", lastCounters.failureSlave[SEQ_ERR_ZZ], counters.failureSlave[SEQ_ERR_ZZ]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/slave/ack_miss", lastCounters.failureSlave[SEQ_ERR_ACK_MISS], counters.failureSlave[SEQ_ERR_ACK_MISS]);
    publishMQTTTopic(initCounters, "ebus/messages/failure/slave/invalid", lastCounters.failureSlave[SEQ_ERR_INVALID], counters.failureSlave[SEQ_ERR_INVALID]);

    publishMQTTTopic(initCounters, "ebus/messages/special/00", lastCounters.special00, counters.special00);
    publishMQTTTopic(initCounters, "ebus/messages/special/0704Success", lastCounters.special0704Success, counters.special0704Success);
    publishMQTTTopic(initCounters, "ebus/messages/special/0704Failure", lastCounters.special0704Failure, counters.special0704Failure);

    lastCounters = counters;
    initCounters = false;
}

// String getLeading(uint8_t byte)
// {
//     std::ostringstream ostr;
//     ostr << std::nouppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
//     return ostr.str().c_str();
// }

// String getSlaves()
// {
//     String s;

//     for (std::map<uint8_t, ebus::Sequence>::iterator it = slaves.begin(); it != slaves.end(); ++it)
//     {
//         s += "\"0x" + String(getLeading(it->first)) + "\":";
//         s += "\"Producer: " + String(getLeading(it->second[1]));
//         s += " Device: " + String(ebus::byte_2_string(it->second.range(2, 5)).c_str());
//         s += " SW: " + String(getLeading(it->second[7])) + "." + String(getLeading(it->second[8]));
//         s += " HW: " + String(getLeading(it->second[9])) + "." + String(getLeading(it->second[10]));
//         s += "\",";
//     }

//     if (slaves.size() > 0)
//         s.remove(s.length() - 1, 1);

//     return s;
// }

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
    doc["last"] = actCommand->last / 1000;

    String payload;
    serializeJson(doc, payload);

    mqttClient.publish(actCommand->topic, 0, true, payload.c_str());
}
