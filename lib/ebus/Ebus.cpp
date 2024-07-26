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

#include "Ebus.h"

const std::vector<uint8_t> ebus::Ebus::push_read(const uint8_t byte)
{
  std::vector<uint8_t> vector;

  if (byte == ebus::seq_syn)
  {
    if (m_seq_read.size() > 0)
    {
     //  m_tel_read = Telegram(m_seq_read);
      vector = m_seq_read.get_sequence();
      m_seq_read.clear();
    }
  }
  else
  {
    m_seq_read.push_back(byte);
  }

  return vector;
}

const std::vector<uint8_t> ebus::Ebus::push_write(const uint8_t byte)
{
  std::vector<uint8_t> vector;

  m_seq_write.push_back(byte);

  if (m_seq_write.size() > 0)
    vector = m_seq_write.get_sequence();

  return vector;
}
