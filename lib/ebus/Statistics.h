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

// This file collects and provides statistical data based on the ebus classes Telegram and Sequence.

#ifndef EBUS_STATISTICS_H
#define EBUS_STATISTICS_H

#include "Telegram.h"

#include <map>

namespace ebus
{

    struct Counter
    {
        unsigned long total = 0;

        unsigned long success = 0;
        float successPercent = 0;

        unsigned long successMS = 0;
        unsigned long successMM = 0;
        unsigned long successBC = 0;

        unsigned long failure = 0;
        float failurePercent = 0;

        std::map<int, unsigned long> failureMaster =
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

        std::map<int, unsigned long> failureSlave =
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

        unsigned long special00 = 0;
        unsigned long special0704Success = 0;
        unsigned long special0704Failure = 0;

        // std::map<uint8_t, ebus::Sequence> masters;
        // std::map<uint8_t, ebus::Sequence> slaves;
    };

    class Statistics
    {

    public:
        Statistics() = default;

        void collect(const uint8_t byte);
        void reset();

        Counter &getCounters();

        // const std::map<uint8_t, ebus::Sequence> getMasters() const;
        // const std::map<uint8_t, ebus::Sequence> getSlaves() const;

    private:
        Sequence sequence;
        Counter counters;
    };

} // namespace ebus

#endif // EBUS_STATISTICS_H