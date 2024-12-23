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

// This file offers various functions for decoding/encoding in accordance with
// the ebus data types and beyond.

#pragma once

#include <stdint.h>
#include <sys/types.h>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace ebus {
// available data types
enum class Datatype {
  ERROR = -1,
  BCD,
  UINT8,
  INT8,
  UINT16,
  INT16,
  UINT32,
  INT32,
  DATA1b,
  DATA1c,
  DATA2b,
  DATA2c,
  FLOAT,
  STRING
};

const char *datatype2string(Datatype datatype);
Datatype string2datatype(const char *str);

// templates for byte / integer conversion
template <typename T>
struct templateType {
  using type = T;
};

template <typename T>
void byte2int(T &t, const std::vector<uint8_t> &bytes) {
  t = 0;

  for (size_t i = 0; i < bytes.size(); i++) t |= bytes[i] << (8 * i);
}

template <typename T>
typename templateType<T>::type byte2int(const std::vector<uint8_t> &bytes) {
  T t;
  byte2int(t, bytes);
  return t;
}

template <typename T>
void int2byte(const T &t, std::vector<uint8_t> &bytes) {
  for (size_t i = 0; i < sizeof(T); i++) bytes.push_back(uint8_t(t >> (8 * i)));
}

template <typename T>
std::vector<uint8_t> int2byte(const T &t) {
  std::vector<uint8_t> bytes;
  int2byte(t, bytes);
  return bytes;
}

// helper functions
uint convert_base(uint value, const uint &oldBase, const uint &newBase);

double_t round_digits(const double_t &value, const uint8_t &digits);

// BCD
uint8_t byte_2_bcd(const std::vector<uint8_t> &bytes);

std::vector<uint8_t> bcd_2_byte(const uint8_t &value);

// uint8_t
uint8_t byte_2_uint8(const std::vector<uint8_t> &bytes);

std::vector<uint8_t> uint8_2_byte(const uint8_t &value);

// int8_t
int8_t byte_2_int8(const std::vector<uint8_t> &bytes);

std::vector<uint8_t> int8_2_byte(const int8_t &value);

// uint16_t
uint16_t byte_2_uint16(const std::vector<uint8_t> &bytes);

std::vector<uint8_t> uint16_2_byte(const uint16_t &value);

// int16_t
int16_t byte_2_int16(const std::vector<uint8_t> &bytes);

std::vector<uint8_t> int16_2_byte(const int16_t &value);

// uint32_t
uint32_t byte_2_uint32(const std::vector<uint8_t> &bytes);

std::vector<uint8_t> uint32_2_byte(const uint32_t &value);

// int32_t
int32_t byte_2_int32(const std::vector<uint8_t> &bytes);

std::vector<uint8_t> int32_2_byte(const int32_t &value);

// DATA1b
double_t byte_2_data1b(const std::vector<uint8_t> &bytes);

std::vector<uint8_t> data1b_2_byte(const double_t &value);

// DATA1c
double_t byte_2_data1c(const std::vector<uint8_t> &bytes);

std::vector<uint8_t> data1c_2_byte(const double_t &value);

// DATA2b
double_t byte_2_data2b(const std::vector<uint8_t> &bytes);

std::vector<uint8_t> data2b_2_byte(const double_t &value);

// DATA2c
double_t byte_2_data2c(const std::vector<uint8_t> &bytes);

std::vector<uint8_t> data2c_2_byte(const double_t &value);

// float
double_t byte_2_float(const std::vector<uint8_t> &bytes);

std::vector<uint8_t> float_2_byte(const double_t &value);

// string
const std::string byte_2_string(const std::vector<uint8_t> &vec);

const std::vector<uint8_t> string_2_byte(const std::string &str);

}  // namespace ebus
