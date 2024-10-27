/*
 * Copyright (C) Roland Jax 2023-2024 <roland.jax@liwest.at>
 *
 * This file is part of ebus.
 *
 * ebus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ebus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ebus. If not, see http://www.gnu.org/licenses/.
 */

#include "EbusStatistics.h"
#include "Datatypes.h"

void ebus::EbusStatistics::collect(const uint8_t byte)
{
    if (byte == ebus::sym_syn)
    {
        if (sequence.size() > 0)
        {
            countTotal++;

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
                {
                    count0704Success++;
                    // slaves[sequence[1]] = tel.getSlave();
                }
                else
                {
                    count0704Failure++;
                }
            }

            sequence.clear();
        }
    }
    else
    {
        sequence.push_back(byte);
    }
}

void ebus::EbusStatistics::reset()
{
    countTotal = 0;
    countSuccess = 0;
    countFailure = 0;

    countMasterSlave = 0;
    countMasterMaster = 0;
    countBroadcast = 0;

    for (std::pair<const int, unsigned long> &item : masterFailure)
        item.second = 0;

    for (std::pair<const int, unsigned long> &item : slaveFailure)
        item.second = 0;

    count00 = 0;
    count0704Success = 0;
    count0704Failure = 0;

    // masters.clear();
    // slaves.clear();
}

unsigned long ebus::EbusStatistics::getTotal() const
{
    return countTotal;
}

unsigned long ebus::EbusStatistics::getSuccess() const
{
    return countSuccess;
}

unsigned long ebus::EbusStatistics::getFailure() const
{
    return countFailure;
}

float ebus::EbusStatistics::getSuccessPercent() const
{
    return countSuccess / (float)countTotal * 100.0f;
}

float ebus::EbusStatistics::getFailurePercent() const
{
    return countFailure / (float)countTotal * 100.0f;
}

unsigned long ebus::EbusStatistics::getMasterSlave() const
{
    return countMasterSlave;
}

unsigned long ebus::EbusStatistics::getMasterMaster() const
{
    return countMasterMaster;
}

unsigned long ebus::EbusStatistics::getBroadcast() const
{
    return countBroadcast;
}

unsigned long ebus::EbusStatistics::getMasterFailure(const int key) const
{
    return masterFailure.at(key);
}

unsigned long ebus::EbusStatistics::getSlaveFailure(const int key) const
{
    return slaveFailure.at(key);
}

unsigned long ebus::EbusStatistics::get00() const
{
    return count00;
}

unsigned long ebus::EbusStatistics::get0704Success() const
{
    return count0704Success;
}

unsigned long ebus::EbusStatistics::get0704Failure() const
{
    return count0704Failure;
}

// const std::map<uint8_t, ebus::Sequence> ebus::EbusStatistics::getMasters() const
// {
//     return masters;
// }

// const std::map<uint8_t, ebus::Sequence> ebus::EbusStatistics::getSlaves() const
// {
//     return slaves;
// }
