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

// This file collects and provides statistical data based on the ebus classes
// Telegram and Sequence.

#ifndef LIB_EBUS_STATISTICS_H_
#define LIB_EBUS_STATISTICS_H_

#include <map>

#include "Telegram.h"

namespace ebus {

struct Counter {
  uint32_t total = 0;

  uint32_t success = 0;
  float successPercent = 0;

  uint32_t successMS = 0;
  uint32_t successMM = 0;
  uint32_t successBC = 0;

  uint32_t failure = 0;
  float failurePercent = 0;

  std::map<int, uint32_t> failureMaster = {
      {SEQ_EMPTY, 0},        {SEQ_OK, 0},         {SEQ_ERR_SHORT, 0},
      {SEQ_ERR_LONG, 0},     {SEQ_ERR_NN, 0},     {SEQ_ERR_CRC, 0},
      {SEQ_ERR_ACK, 0},      {SEQ_ERR_QQ, 0},     {SEQ_ERR_ZZ, 0},
      {SEQ_ERR_ACK_MISS, 0}, {SEQ_ERR_INVALID, 0}};

  std::map<int, uint32_t> failureSlave = {
      {SEQ_EMPTY, 0},        {SEQ_OK, 0},         {SEQ_ERR_SHORT, 0},
      {SEQ_ERR_LONG, 0},     {SEQ_ERR_NN, 0},     {SEQ_ERR_CRC, 0},
      {SEQ_ERR_ACK, 0},      {SEQ_ERR_QQ, 0},     {SEQ_ERR_ZZ, 0},
      {SEQ_ERR_ACK_MISS, 0}, {SEQ_ERR_INVALID, 0}};

  uint32_t special00 = 0;
  uint32_t special0704Success = 0;
  uint32_t special0704Failure = 0;

  // std::map<uint8_t, ebus::Sequence> masters;
  // std::map<uint8_t, ebus::Sequence> slaves;
};

class Statistics {
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

}  // namespace ebus

#endif  // LIB_EBUS_STATISTICS_H_
