#include "statistic.hpp"
#include "Telegram.h"

#include <map>

ebus::Sequence sequence;

unsigned long countReceived = 0;
unsigned long countSuccess = 0;
unsigned long countFailure = 0;

unsigned long countMasterSlave = 0;
unsigned long countMasterMaster = 0;
unsigned long countBroadcast = 0;

std::map<int, unsigned long> masterFailure =
    {{SEQ_EMPTY, 0},
     {SEQ_OK, 0},
     {SEQ_ERR_SHORT, 0},
     {SEQ_ERR_LONG, 0},
     {SEQ_ERR_NN, 0},
     {SEQ_ERR_CRC, 0},
     {SEQ_ERR_ACK, 0},
     {SEQ_ERR_QQ, 0},
     {SEQ_ERR_ZZ, 0},
     {SEQ_ERR_ACK_MISS, 0},
     {SEQ_ERR_INVALID, 0}};

std::map<int, unsigned long> slaveFailure =
    {{SEQ_EMPTY, 0},
     {SEQ_OK, 0},
     {SEQ_ERR_SHORT, 0},
     {SEQ_ERR_LONG, 0},
     {SEQ_ERR_NN, 0},
     {SEQ_ERR_CRC, 0},
     {SEQ_ERR_ACK, 0},
     {SEQ_ERR_QQ, 0},
     {SEQ_ERR_ZZ, 0},
     {SEQ_ERR_ACK_MISS, 0},
     {SEQ_ERR_INVALID, 0}};

unsigned long count00 = 0;
unsigned long count0704Success = 0;
unsigned long count0704Failure = 0;

void resetStatistic()
{
    countReceived = 0;
    countSuccess = 0;
    countFailure = 0;

    countMasterSlave = 0;
    countMasterMaster = 0;
    countBroadcast = 0;

    for (std::map<int, unsigned long>::iterator it = masterFailure.begin(); it != masterFailure.end(); ++it)
        it->second = 0;

    for (std::map<int, unsigned long>::iterator it = slaveFailure.begin(); it != slaveFailure.end(); ++it)
        it->second = 0;

    count00 = 0;
    count0704Success = 0;
    count0704Failure = 0;
}

void collectStatistic(const uint8_t byte)
{
    if (byte == ebus::sym_syn)
    {
        if (sequence.size() > 0)
        {
            countReceived++;

            ebus::Telegram tel(sequence);

            if (tel.isValid())
            {
                countSuccess++;

                if (tel.get_type() == ebus::Type::MS)
                    countMasterSlave++;
                else if (tel.get_type() == ebus::Type::MM)
                    countMasterMaster++;
                else if (tel.get_type() == ebus::Type::BC)
                    countBroadcast++;
            }
            else
            {
                countFailure++;

                masterFailure[tel.getMasterState()]++;
                slaveFailure[tel.getSlaveState()]++;
            }

            if (sequence.size() == 1 && sequence[0] == 0x00)
            {
                count00++;
            }
            else if (sequence.size() >= 3 && sequence[2] == 0x07 && sequence[3] == 0x04)
            {
                if (sequence.size() > 6)
                    count0704Success++;
                else
                    count0704Failure++;
            }

            sequence.clear();
        }
    }
    else
    {
        sequence.push_back(byte);
    }
}

String printCommandJsonStatistic()
{
    String s = "{\"esp-eBus\":{\"Telegrams\":{";
    s += "\"count_received\":" + String(countReceived) + ",";
    s += "\"count_success\":" + String(countSuccess) + ",";
    s += "\"count_failure\":" + String(countFailure) + ",";
    s += "\"percent_success\":" + String(countSuccess / (float)countReceived * 100.0f) + ",";
    s += "\"percent_failure\":" + String(countFailure / (float)countReceived * 100.0f) + "},";
    s += "\"Type_Success\":{";
    s += "\"count_master_slave\":" + String(countMasterSlave) + ",";
    s += "\"count_master_master\":" + String(countMasterMaster) + ",";
    s += "\"count_broadcast\":" + String(countBroadcast) + "},";
    s += "\"Master_Failure\":{";
    s += "\"master_empty\":" + String(masterFailure[SEQ_EMPTY]) + ",";
    s += "\"master_ok\":" + String(masterFailure[SEQ_OK]) + ",";
    s += "\"master_short\":" + String(masterFailure[SEQ_ERR_SHORT]) + ",";
    s += "\"master_long\":" + String(masterFailure[SEQ_ERR_LONG]) + ",";
    s += "\"master_nn\":" + String(masterFailure[SEQ_ERR_NN]) + ",";
    s += "\"master_crc\":" + String(masterFailure[SEQ_ERR_CRC]) + ",";
    s += "\"master_ack\":" + String(masterFailure[SEQ_ERR_ACK]) + ",";
    s += "\"master_qq\":" + String(masterFailure[SEQ_ERR_QQ]) + ",";
    s += "\"master_zz\":" + String(masterFailure[SEQ_ERR_ZZ]) + ",";
    s += "\"master_ack_miss\":" + String(masterFailure[SEQ_ERR_ACK_MISS]) + ",";
    s += "\"master_invalid\":" + String(masterFailure[SEQ_ERR_INVALID]) + "},";
    s += "\"Slave_Failure\":{";
    s += "\"slave_empty\":" + String(slaveFailure[SEQ_EMPTY]) + ",";
    s += "\"slave_ok\":" + String(slaveFailure[SEQ_OK]) + ",";
    s += "\"slave_short\":" + String(slaveFailure[SEQ_ERR_SHORT]) + ",";
    s += "\"slave_long\":" + String(slaveFailure[SEQ_ERR_LONG]) + ",";
    s += "\"slave_nn\":" + String(slaveFailure[SEQ_ERR_NN]) + ",";
    s += "\"slave_crc\":" + String(slaveFailure[SEQ_ERR_CRC]) + ",";
    s += "\"slave_ack\":" + String(slaveFailure[SEQ_ERR_ACK]) + ",";
    s += "\"slave_qq\":" + String(slaveFailure[SEQ_ERR_QQ]) + ",";
    s += "\"slave_zz\":" + String(slaveFailure[SEQ_ERR_ZZ]) + ",";
    s += "\"slave_ack_miss\":" + String(slaveFailure[SEQ_ERR_ACK_MISS]) + ",";
    s += "\"slave_invalid\":" + String(slaveFailure[SEQ_ERR_INVALID]) + "},";
    s += "\"Special\":{";
    s += "\"00\":" + String(count00) + ",";
    s += "\"0704_success\":" + String(count0704Success) + ",";
    s += "\"0704_failure\":" + String(count0704Failure) + "";
    s += "}}}";

    return s;
}
