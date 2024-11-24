/*
 * Copyright (C) Roland Jax 2012-2024 <roland.jax@liwest.at>
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

// This class implements basic routines for sequence handling in accordance with
// the ebus specification, in particular the reduction, extension and
// crc-calculation.
//
// (reduced) 0xaa <-> 0xa9 0x01 (expanded)
// (reduced) 0xa9 <-> 0xa9 0x00 (expanded)

#ifndef LIB_EBUS_SEQUENCE_H_
#define LIB_EBUS_SEQUENCE_H_

#include <stdint.h>

#include <cstddef>
#include <string>
#include <vector>

namespace ebus {

static const uint8_t sym_zero = 0x00;    // zero byte
static const uint8_t sym_syn = 0xaa;     // synchronization byte
static const uint8_t sym_exp = 0xa9;     // expand byte
static const uint8_t sym_synexp = 0x01;  // expanded synchronization byte
static const uint8_t sym_expexp = 0x00;  // expanded expand byte

class Sequence {
 public:
  Sequence() = default;
  Sequence(const Sequence &seq, const size_t index, size_t len = 0);

  void assign(const std::vector<uint8_t> &vec, const bool extended = true);

  void push_back(const uint8_t byte, const bool extended = true);

  const uint8_t &operator[](const size_t index) const;
  const std::vector<uint8_t> range(const size_t index, const size_t len) const;

  size_t size() const;

  void clear();

  uint8_t crc();

  void extend();
  void reduce();

  const std::string to_string() const;
  const std::vector<uint8_t> &to_vector() const;

  static const std::vector<uint8_t> range(const std::vector<uint8_t> &vec,
                                          const size_t index, const size_t len);

  static const std::vector<uint8_t> to_vector(const std::string &str);

  static const std::string to_string(const std::vector<uint8_t> &vec);

  static bool contains(const std::vector<uint8_t> &vec,
                       const std::vector<uint8_t> &search);

 private:
  std::vector<uint8_t> m_seq;

  bool m_extended = false;

  static uint8_t calc_crc(const uint8_t byte, const uint8_t init);
};

}  // namespace ebus

#endif  // LIB_EBUS_SEQUENCE_H_
