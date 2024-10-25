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

// Implementation of the sending routines for Master-Slave telegrams
// based on the ebus classes Telegram and Sequence.

#ifndef EBUS_EBUSHANDLER_H
#define EBUS_EBUSHANDLER_H

#include "Telegram.h"

#include <functional>

namespace ebus
{

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

    class EbusHandler
    {

    public:
        EbusHandler() = default;
        EbusHandler(const uint8_t source,
                    std::function<bool()> busReadyFunction,
                    std::function<void(const uint8_t byte)> busWriteFunction,
                    std::function<void(const std::vector<uint8_t> response)> responseFunction);

        State getState() const;
        uint8_t getAddress() const;

        void reset();
        bool enque(const std::vector<uint8_t> &message);

        void send();
        bool receive(const uint8_t byte);

    private:
        uint8_t address;

        std::function<bool()> busReadyCallback = nullptr;
        std::function<void(const uint8_t byte)> busWriteCallback = nullptr;
        std::function<void(const std::vector<uint8_t> response)> responseCallback = nullptr;

        State state = State::MonitorBus;

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
    };

} // namespace ebus

#endif // EBUS_EBUSHANDLER_H
