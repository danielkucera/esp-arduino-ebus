/*
 * Copyright (C) Roland Jax 2012-2023 <roland.jax@liwest.at>
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

#ifndef EBUS_EBUS_H
#define EBUS_EBUS_H

#include "Sequence.h"

namespace ebus
{

// enum class State
// {
// 	MonitorBus,
// 	LockBus,
// 	SendMessage,
// 	ReceiveResponse,
// 	ReceiveMessage,
// 	ProcessMessage,
// 	SendResponse,
// 	FreeBus
// };

class Ebus
{

public:
	Ebus() = default;

    const std::vector<uint8_t> push_read(const uint8_t byte);
    const std::vector<uint8_t> push_write(const uint8_t byte);

    inline void clear_write()
    {
        m_seq_write.clear();
    }

    inline const std::string to_string_write() const
    {
        return m_seq_write.to_string();
    }

    inline size_t size_of_write() const
    {
        return m_seq_write.size();
    }

private:
    // State m_state = State::MonitorBus;

    Sequence m_seq_read;
    Sequence m_seq_write;
};   

} // namespace ebus

#endif // EBUS_EBUS_H