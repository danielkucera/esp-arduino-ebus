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
        ebusStatistics.collect(byte);

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
    ebusStatistics.reset();
}

void Schedule::publishMQTT()
{
    mqttClient.publish("ebus/messages/total", 0, true, String(ebusStatistics.getTotal()).c_str());

    mqttClient.publish("ebus/messages/success", 0, true, String(ebusStatistics.getSuccess()).c_str());
    mqttClient.publish("ebus/messages/success/percent", 0, true, String(ebusStatistics.getSuccessPercent()).c_str());
    mqttClient.publish("ebus/messages/success/master_slave", 0, true, String(ebusStatistics.getSuccessMasterSlave()).c_str());
    mqttClient.publish("ebus/messages/success/master_master", 0, true, String(ebusStatistics.getSuccessMasterMaster()).c_str());
    mqttClient.publish("ebus/messages/success/broadcast", 0, true, String(ebusStatistics.getSuccessBroadcast()).c_str());

    mqttClient.publish("ebus/messages/failure", 0, true, String(ebusStatistics.getFailure()).c_str());
    mqttClient.publish("ebus/messages/failure/percent", 0, true, String(ebusStatistics.getFailurePercent()).c_str());

    mqttClient.publish("ebus/messages/failure/master/empty", 0, true, String(ebusStatistics.getMasterFailure(SEQ_EMPTY)).c_str());
    mqttClient.publish("ebus/messages/failure/master/ok", 0, true, String(ebusStatistics.getMasterFailure(SEQ_OK)).c_str());
    mqttClient.publish("ebus/messages/failure/master/short", 0, true, String(ebusStatistics.getMasterFailure(SEQ_ERR_SHORT)).c_str());
    mqttClient.publish("ebus/messages/failure/master/long", 0, true, String(ebusStatistics.getMasterFailure(SEQ_ERR_LONG)).c_str());
    mqttClient.publish("ebus/messages/failure/master/nn", 0, true, String(ebusStatistics.getMasterFailure(SEQ_ERR_NN)).c_str());
    mqttClient.publish("ebus/messages/failure/master/crc", 0, true, String(ebusStatistics.getMasterFailure(SEQ_ERR_CRC)).c_str());
    mqttClient.publish("ebus/messages/failure/master/ack", 0, true, String(ebusStatistics.getMasterFailure(SEQ_ERR_ACK)).c_str());
    mqttClient.publish("ebus/messages/failure/master/qq", 0, true, String(ebusStatistics.getMasterFailure(SEQ_ERR_QQ)).c_str());
    mqttClient.publish("ebus/messages/failure/master/zz", 0, true, String(ebusStatistics.getMasterFailure(SEQ_ERR_ZZ)).c_str());
    mqttClient.publish("ebus/messages/failure/master/ack_miss", 0, true, String(ebusStatistics.getMasterFailure(SEQ_ERR_ACK_MISS)).c_str());
    mqttClient.publish("ebus/messages/failure/master/invalid", 0, true, String(ebusStatistics.getMasterFailure(SEQ_ERR_INVALID)).c_str());

    mqttClient.publish("ebus/messages/failure/slave/empty", 0, true, String(ebusStatistics.getSlaveFailure(SEQ_EMPTY)).c_str());
    mqttClient.publish("ebus/messages/failure/slave/ok", 0, true, String(ebusStatistics.getSlaveFailure(SEQ_OK)).c_str());
    mqttClient.publish("ebus/messages/failure/slave/short", 0, true, String(ebusStatistics.getSlaveFailure(SEQ_ERR_SHORT)).c_str());
    mqttClient.publish("ebus/messages/failure/slave/long", 0, true, String(ebusStatistics.getSlaveFailure(SEQ_ERR_LONG)).c_str());
    mqttClient.publish("ebus/messages/failure/slave/nn", 0, true, String(ebusStatistics.getSlaveFailure(SEQ_ERR_NN)).c_str());
    mqttClient.publish("ebus/messages/failure/slave/crc", 0, true, String(ebusStatistics.getSlaveFailure(SEQ_ERR_CRC)).c_str());
    mqttClient.publish("ebus/messages/failure/slave/ack", 0, true, String(ebusStatistics.getSlaveFailure(SEQ_ERR_ACK)).c_str());
    mqttClient.publish("ebus/messages/failure/slave/qq", 0, true, String(ebusStatistics.getSlaveFailure(SEQ_ERR_QQ)).c_str());
    mqttClient.publish("ebus/messages/failure/slave/zz", 0, true, String(ebusStatistics.getSlaveFailure(SEQ_ERR_ZZ)).c_str());
    mqttClient.publish("ebus/messages/failure/slave/ack_miss", 0, true, String(ebusStatistics.getSlaveFailure(SEQ_ERR_ACK_MISS)).c_str());
    mqttClient.publish("ebus/messages/failure/slave/invalid", 0, true, String(ebusStatistics.getSlaveFailure(SEQ_ERR_INVALID)).c_str());

    mqttClient.publish("ebus/messages/special/00", 0, true, String(ebusStatistics.get00()).c_str());
    mqttClient.publish("ebus/messages/special/0704_success", 0, true, String(ebusStatistics.get0704Success()).c_str());
    mqttClient.publish("ebus/messages/special/0704_failure", 0, true, String(ebusStatistics.get0704Failure()).c_str());
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
