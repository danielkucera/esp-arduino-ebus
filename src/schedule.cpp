#include "schedule.hpp"

#include <sstream>
#include <iomanip>

#include "Datatypes.h"
#include "Sequence.h"
#include "Telegram.h"

#include "bus.hpp"

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



enum class State
{
    MonitorBus,
    Arbitration,
    SendMessage,
    ReceiveAcknowledge,
    ReceiveResponse,
    SendPositiveAcknowledge,
    SendNegativeAcknowledge,
    FreeBus
};

State state = State::MonitorBus;

size_t commandIndex = 0;
unsigned long commandCounter = 0;

unsigned long millisLastCommand = 0;
unsigned long distanceCommands = 10 * 1000; // TODO Systemparameter ?

uint8_t QQ = 0xff; // TODO Systemparameter ?

WiFiClient *dummyClient = new WiFiClient();

ebus::Telegram telegram;

ebus::Sequence master;
size_t sendIndex = 0;
size_t receiveIndex = 0;
bool masterRepeated = false;

ebus::Sequence slave;
size_t slaveIndex = 0;
size_t slaveNN = 0;
bool slaveRepeated = false;

bool sendAcknowledge = true;
bool sendSyn = true;

void saveCommandValue(datatype type, ebus::Sequence seq)
{
    switch (commandTable[commandIndex].type)
    {
    case datatype::bcd:
        commandTable[commandIndex].uvalue = ebus::byte_2_bcd(seq.range(1, 1));
        break;
    case datatype::uch:
        commandTable[commandIndex].uvalue = ebus::byte_2_uint8(telegram.getSlave().range(1, 1));
        break;
    case datatype::sch:
        commandTable[commandIndex].ivalue = ebus::byte_2_int8(telegram.getSlave().range(1, 1));
        break;
    case datatype::uin:
        commandTable[commandIndex].uvalue = ebus::byte_2_uint16(telegram.getSlave().range(1, 2));
        break;
    case datatype::sin:
        commandTable[commandIndex].ivalue = ebus::byte_2_int16(telegram.getSlave().range(1, 2));
        break;
    case datatype::ulg:
        commandTable[commandIndex].uvalue = ebus::byte_2_uint32(telegram.getSlave().range(1, 4));
        break;
    case datatype::slg:
        commandTable[commandIndex].ivalue = ebus::byte_2_int32(telegram.getSlave().range(1, 4));
        break;
    case datatype::d1b:
        commandTable[commandIndex].dvalue = ebus::byte_2_data1b(telegram.getSlave().range(1, 1));
        break;
    case datatype::d1c:
        commandTable[commandIndex].dvalue = ebus::byte_2_data1c(telegram.getSlave().range(1, 1));
        break;
    case datatype::d2b:
        commandTable[commandIndex].dvalue = ebus::byte_2_data2b(telegram.getSlave().range(1, 2));
        break;
    case datatype::d2c:
        commandTable[commandIndex].dvalue = ebus::byte_2_data2c(telegram.getSlave().range(1, 2));
        break;
    case datatype::flt:
        commandTable[commandIndex].dvalue = ebus::byte_2_float(telegram.getSlave().range(1, 2));
        break;
    default:
        break;
    }
}

std::string printCommandDescription(size_t index)
{
    std::ostringstream ostr;
    ostr << commandTable[index].desc << " " << commandTable[index].unit;
    return ostr.str();
}

std::string printCommandValue(size_t index)
{
    std::ostringstream ostr;

    switch (commandTable[index].type)
    {
    case datatype::bcd:
    case datatype::uch:
    case datatype::uin:
    case datatype::ulg:
        ostr << commandTable[index].uvalue;
        break;
    case datatype::sch:
    case datatype::sin:
    case datatype::slg:
        ostr << commandTable[index].ivalue;
        break;
    case datatype::d1b:
    case datatype::d1c:
    case datatype::d2b:
    case datatype::d2c:
    case datatype::flt:
        ostr << commandTable[index].dvalue;
        break;
    default:
        break;
    }

    return ostr.str();
}

std::string escape_json(const std::string &s)
{
    std::ostringstream o;
    for (auto c = s.cbegin(); c != s.cend(); c++)
    {
        if (*c == '"' || *c == '\\' || ('\x00' <= *c && *c <= '\x1f'))
        {
            o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(*c);
        }
        else
        {
            o << *c;
        }
    }
    return o.str();
}

void resetData()
{
    telegram.clear();

    master.clear();
    sendIndex = 0;
    receiveIndex = 0;
    masterRepeated = false;

    slave.clear();
    slaveIndex = 0;
    slaveNN = 0;
    slaveRepeated = false;

    sendAcknowledge = true;
    sendSyn = true;
}

Command nextCommand()
{
    commandCounter++;
    commandIndex++;

    if (commandIndex >= commandTable.size())
        commandIndex = 0;

    return commandTable[commandIndex];
}

bool handleSchedule()
{
    if (commandTable.size() == 0)
        return false;

    u_int8_t byte;

    switch (state)
    {
    case State::MonitorBus:
        if (millis() > millisLastCommand + distanceCommands)
        {
            millisLastCommand = millis();

            resetData();
            telegram.createMaster(QQ, ebus::Sequence::to_vector(nextCommand().data));

            if (telegram.getMasterState() == SEQ_OK)
            {
                master = telegram.getMaster();
                master.push_back(telegram.getMasterCRC(), false);
                master.extend();

                // start arbitration
                WiFiClient *cl = dummyClient;
                uint8_t ad = master[receiveIndex];

                if (!setArbitrationClient(cl, ad))
                {
                    if (cl != dummyClient)
                    {
                        DEBUG_LOG("ARBITRATION ONGOING 0x%02 0x%02x\n", ad, master[receiveIndex]);
                    }
                }
                else
                {
                    DEBUG_LOG("ARBITRATION START 0x%02x\n", ad);
                }
            }
            state = State::Arbitration;
        }
        break;
    case State::Arbitration:
        break;
    case State::SendMessage:
        if (Bus.availableForWrite() && sendIndex == receiveIndex)
        {
            byte = master[sendIndex];
            DEBUG_LOG("SEND 0x%02x\n", byte);
            Bus.write(byte);
            sendIndex++;
        }
        break;
    case State::ReceiveAcknowledge:
        break;
    case State::ReceiveResponse:
        break;
    case State::SendPositiveAcknowledge:
        if (Bus.availableForWrite() && sendAcknowledge)
        {
            sendAcknowledge = false;
            byte = ebus::seq_ack;
            DEBUG_LOG("SEND ACK 0x%02x\n", byte);
            Bus.write(byte);
        }
        break;
    case State::SendNegativeAcknowledge:
        if (Bus.availableForWrite() && sendAcknowledge)
        {
            sendAcknowledge = false;
            byte = ebus::seq_nak;
            DEBUG_LOG("SEND NAK 0x%02x\n", byte);
            Bus.write(byte);
        }
        break;
    case State::FreeBus:
        if (Bus.availableForWrite() && sendSyn)
        {
            sendSyn = false;
            byte = ebus::seq_syn;
            DEBUG_LOG("SEND SYN 0x%02x\n", byte);
            Bus.write(byte);
        }
        break;
    default:
        break;
    }

    return true;
}

bool pushSchedule(bool enhanced, WiFiClient *client, const uint8_t byte)
{
    if (commandTable.size() == 0)
        return false;

    switch (state)
    {
    case State::MonitorBus:
        break;
    case State::Arbitration:
        // workaround - master sequence comes twice - waiting for second master sequence without "enhanced" flag
        if (!enhanced)
        {
            if (client == dummyClient)
            {
                if (byte == master[receiveIndex])
                {
                    sendIndex = 1;
                    receiveIndex = 1;
                    state = State::SendMessage;
                }
            }
        }

        // reset - if arbitration went wrong
        if (millis() > millisLastCommand + (distanceCommands / 4))
        {
            state = State::MonitorBus;
        }
        break;
    case State::SendMessage:
        receiveIndex++;
        if (receiveIndex >= master.size())
            state = State::ReceiveAcknowledge;
        break;
    case State::ReceiveAcknowledge:
        DEBUG_LOG("RECEIVE 0x%02x\n", byte);
        if (byte == ebus::seq_ack)
        {
            state = State::ReceiveResponse;
        }
        else if (!masterRepeated)
        {
            masterRepeated = true;
            sendIndex = 1;
            receiveIndex = 1;
            state = State::SendMessage;
        }
        else
        {
            state = State::FreeBus;
        }
        break;
    case State::ReceiveResponse:
        DEBUG_LOG("RECEIVE 0x%02x\n", byte);
        slaveIndex++;
        slave.push_back(byte);

        if (slave.size() == 1)
            slaveNN = 1 + int(byte) + 1; // NN + Dx + CRC

        if (byte == ebus::seq_exp) // AA >> A9 + 01 || A9 >> A9 + 00
            slaveNN++;

        if (slave.size() >= slaveNN)
        {
            telegram.createSlave(slave);
            if (telegram.getSlaveState() == SEQ_OK)
            {
                sendAcknowledge = true;
                state = State::SendPositiveAcknowledge;

                saveCommandValue(commandTable[commandIndex].type, telegram.getSlave());
            }
            else
            {
                sendAcknowledge = true;
                state = State::SendNegativeAcknowledge;
            }
        }

        break;
    case State::SendPositiveAcknowledge:
        state = State::FreeBus;
        break;
    case State::SendNegativeAcknowledge:
        if (!slaveRepeated)
        {
            slaveRepeated = true;
            state = State::ReceiveResponse;
        }
        else
        {
            state = State::FreeBus;
        }
        break;
    case State::FreeBus:
        state = State::MonitorBus;
        break;
    default:
        break;
    }

    return true;
}

size_t printCommandState()
{
    return static_cast<size_t>(state);
}

unsigned long printCommandCounter()
{
    return commandCounter;
}

size_t printCommandIndex()
{
    return commandIndex;
}

String printCommandMaster()
{
    return escape_json(master.to_string()).c_str();
}

size_t printCommandMasterSize()
{
    return master.size();
}

size_t printCommandMasterSendIndex()
{
    return sendIndex;
}

size_t printCommandMasterRecvIndex()
{
    return receiveIndex;
}

size_t printCommandMasterState()
{
    return telegram.getMasterState();
}

String printCommandSlave()
{
    return escape_json(slave.to_string()).c_str();
}

size_t printCommandSlaveSize()
{
    return slave.size();
}

size_t printCommandSlaveIndex()
{
    return slaveIndex;
}

size_t printCommandSlaveState()
{
    return telegram.getSlaveState();
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
