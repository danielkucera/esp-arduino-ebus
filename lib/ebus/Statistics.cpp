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

#include "Statistics.h"

#include <utility>

#include "Datatypes.h"

void ebus::Statistics::collect(const uint8_t byte) {
  if (byte == ebus::sym_syn) {
    if (sequence.size() > 0) {
      counters.total++;

      ebus::Telegram tel(sequence);

      if (tel.isValid()) {
        counters.success++;

        if (tel.get_type() == ebus::Type::MS)
          counters.successMS++;
        else if (tel.get_type() == ebus::Type::MM)
          counters.successMM++;
        else if (tel.get_type() == ebus::Type::BC)
          counters.successBC++;
      } else {
        counters.failure++;

        counters.failureMaster[tel.getMasterState()]++;
        counters.failureSlave[tel.getSlaveState()]++;
      }

      counters.successPercent =
          counters.success / static_cast<float>(counters.total) * 100.0f;
      counters.failurePercent =
          counters.failure / static_cast<float>(counters.total) * 100.0f;

      if (sequence.size() == 1 && sequence[0] == 0x00) {
        counters.special00++;
      } else if (sequence.size() >= 3 && sequence[2] == 0x07 &&
                 sequence[3] == 0x04) {
        if (sequence.size() > 6) {
          counters.special0704Success++;
          // slaves[sequence[1]] = tel.getSlave();
        } else {
          counters.special0704Failure++;
        }
      }

      sequence.clear();
    }
  } else {
    sequence.push_back(byte);
  }
}

void ebus::Statistics::reset() {
  counters.total = 0;

  counters.success = 0;
  counters.successMS = 0;
  counters.successMM = 0;
  counters.successBC = 0;

  counters.failure = 0;

  for (std::pair<const int, uint32_t> &item : counters.failureMaster)
    item.second = 0;

  for (std::pair<const int, uint32_t> &item : counters.failureSlave)
    item.second = 0;

  counters.special00 = 0;
  counters.special0704Success = 0;
  counters.special0704Failure = 0;

  // masters.clear();
  // slaves.clear();
}

ebus::Counter &ebus::Statistics::getCounters() { return counters; }
