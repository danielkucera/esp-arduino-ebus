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

// Implementation of the sending routines for Master-Slave telegrams based on
// the ebus classes Telegram and Sequence. It also collects statistical data
// about the ebus system.

#ifndef LIB_EBUS_EBUSHANDLER_H_
#define LIB_EBUS_EBUSHANDLER_H_

#include <functional>
#include <map>
#include <vector>

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
};

enum class State {
  MonitorBus,
  Arbitration,
  SendMessage,
  ReceiveAcknowledge,
  ReceiveResponse,
  SendPositiveAcknowledge,
  SendNegativeAcknowledge,
  FreeBus
};

class EbusHandler {
 public:
  EbusHandler() = default;
  EbusHandler(
      const uint8_t source, std::function<bool()> busReadyFunction,
      std::function<void(const uint8_t byte)> busWriteFunction,
      std::function<void(const std::vector<uint8_t> slave)> responseFunction,
      std::function<void(const std::vector<uint8_t> master,
                         const std::vector<uint8_t> slave)>
          telegramFunction);

  void setAddress(const uint8_t source);
  uint8_t getAddress() const;

  State getState() const;

  void reset();
  bool enque(const std::vector<uint8_t> &message);

  void send();
  void receive(const uint8_t byte);

  void feedCounters(const uint8_t byte);
  void resetCounters();
  Counter &getCounters();

 private:
  uint8_t address = 0;

  std::function<bool()> busReadyCallback = nullptr;
  std::function<void(const uint8_t byte)> busWriteCallback = nullptr;
  std::function<void(const std::vector<uint8_t> slave)> responseCallback =
      nullptr;
  std::function<void(const std::vector<uint8_t> master,
                     const std::vector<uint8_t> slave)>
      telegramCallback = nullptr;

  State state = State::MonitorBus;

  Sequence sequence;
  Counter counters;

  Telegram telegram;

  Sequence master;
  size_t sendIndex = 0;
  size_t receiveIndex = 0;
  bool masterRepeated = false;

  Sequence slave;
  size_t slaveIndex = 0;
  size_t slaveNN = 0;
  bool slaveRepeated = false;

  bool sendAcknowledge = true;
  bool sendSyn = true;
};

}  // namespace ebus

#endif  // LIB_EBUS_EBUSHANDLER_H_
