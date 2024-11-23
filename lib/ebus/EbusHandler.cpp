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

#include "EbusHandler.h"

#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

#include "Telegram.h"

ebus::EbusHandler::EbusHandler(
    const uint8_t source, std::function<bool()> busReadyFunction,
    std::function<void(const uint8_t byte)> busWriteFunction,
    std::function<void(const std::vector<uint8_t> response)> responseFunction)
    : address(source),
      busReadyCallback(busReadyFunction),
      busWriteCallback(busWriteFunction),
      responseCallback(responseFunction) {}

void ebus::EbusHandler::setAddress(const uint8_t source) { address = source; }

uint8_t ebus::EbusHandler::getAddress() const { return address; }

ebus::State ebus::EbusHandler::getState() const { return state; }

void ebus::EbusHandler::reset() {
  state = State::MonitorBus;

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

bool ebus::EbusHandler::enque(const std::vector<uint8_t> &message) {
  reset();
  telegram.createMaster(address, message);
  if (telegram.getMasterState() == SEQ_OK) {
    master = telegram.getMaster();
    master.push_back(telegram.getMasterCRC(), false);
    master.extend();
    state = State::Arbitration;
    return true;
  }
  return false;
}

void ebus::EbusHandler::send() {
  switch (state) {
    case State::MonitorBus:
      break;
    case State::Arbitration:
      break;
    case State::SendMessage:
      if (busReadyCallback() && sendIndex == receiveIndex) {
        busWriteCallback(master[sendIndex]);
        sendIndex++;
      }
      break;
    case State::ReceiveAcknowledge:
      break;
    case State::ReceiveResponse:
      break;
    case State::SendPositiveAcknowledge:
      if (busReadyCallback() && sendAcknowledge) {
        sendAcknowledge = false;
        busWriteCallback(ebus::sym_ack);
      }
      break;
    case State::SendNegativeAcknowledge:
      if (busReadyCallback() && sendAcknowledge) {
        sendAcknowledge = false;
        busWriteCallback(ebus::sym_nak);
      }
      break;
    case State::FreeBus:
      if (busReadyCallback() && sendSyn) {
        sendSyn = false;
        busWriteCallback(ebus::sym_syn);
      }
      break;
    default:
      break;
  }
}

bool ebus::EbusHandler::receive(const uint8_t byte) {
  switch (state) {
    case State::MonitorBus:
      break;
    case State::Arbitration:
      if (byte == address) {
        sendIndex = 1;
        receiveIndex = 1;
        state = State::SendMessage;
      }
      break;
    case State::SendMessage:
      receiveIndex++;
      if (receiveIndex >= master.size()) state = State::ReceiveAcknowledge;
      break;
    case State::ReceiveAcknowledge:
      if (byte == ebus::sym_ack) {
        state = State::ReceiveResponse;
      } else if (!masterRepeated) {
        masterRepeated = true;
        sendIndex = 1;
        receiveIndex = 1;
        state = State::SendMessage;
      } else {
        state = State::FreeBus;
      }
      break;
    case State::ReceiveResponse:
      slaveIndex++;
      slave.push_back(byte);

      if (slave.size() == 1)
        slaveNN = 1 + static_cast<int>(byte) + 1;  // NN + DBx + CRC

      if (byte == ebus::sym_exp)  // AA >> A9 + 01 || A9 >> A9 + 00
        slaveNN++;

      if (slave.size() >= slaveNN) {
        telegram.createSlave(slave);
        if (telegram.getSlaveState() == SEQ_OK) {
          sendAcknowledge = true;
          state = State::SendPositiveAcknowledge;

          responseCallback(telegram.getSlave().to_vector());
        } else {
          slaveIndex = 0;
          slave.clear();
          sendAcknowledge = true;
          state = State::SendNegativeAcknowledge;
        }
      }

      break;
    case State::SendPositiveAcknowledge:
      state = State::FreeBus;
      break;
    case State::SendNegativeAcknowledge:
      if (!slaveRepeated) {
        slaveRepeated = true;
        state = State::ReceiveResponse;
      } else {
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

void ebus::EbusHandler::monitor(const uint8_t byte) {
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

void ebus::EbusHandler::resetCounters() {
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

ebus::Counter &ebus::EbusHandler::getCounters() { return counters; }
