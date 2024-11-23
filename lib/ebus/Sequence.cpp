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

#include "Sequence.h"

#include <iomanip>
#include <sstream>

ebus::Sequence::Sequence(const Sequence &seq, const size_t index, size_t len) {
  if (len == 0) len = seq.size() - index;

  for (size_t i = index; i < index + len; i++) m_seq.push_back(seq.m_seq.at(i));

  m_extended = seq.m_extended;
}

void ebus::Sequence::assign(const std::vector<uint8_t> &vec,
                            const bool extended) {
  clear();

  for (size_t i = 0; i < vec.size(); i++) push_back(vec[i], extended);
}

void ebus::Sequence::push_back(const uint8_t byte, const bool extended) {
  m_seq.push_back(byte);
  m_extended = extended;
}

const uint8_t &ebus::Sequence::operator[](const size_t index) const {
  return m_seq.at(index);
}

const std::vector<uint8_t> ebus::Sequence::range(const size_t index,
                                                 const size_t len) const {
  return range(m_seq, index, len);
}

size_t ebus::Sequence::size() const { return m_seq.size(); }

void ebus::Sequence::clear() {
  m_seq.clear();
  m_seq.shrink_to_fit();
  m_extended = false;
}

uint8_t ebus::Sequence::crc() {
  if (!m_extended) extend();

  uint8_t crc = sym_zero;

  for (size_t i = 0; i < m_seq.size(); i++) crc = calc_crc(m_seq.at(i), crc);

  reduce();

  return crc;
}

void ebus::Sequence::extend() {
  if (m_extended) return;

  std::vector<uint8_t> tmp;

  for (size_t i = 0; i < m_seq.size(); i++) {
    if (m_seq.at(i) == sym_syn) {
      tmp.push_back(sym_exp);
      tmp.push_back(sym_synexp);
    } else if (m_seq.at(i) == sym_exp) {
      tmp.push_back(sym_exp);
      tmp.push_back(sym_expexp);
    } else {
      tmp.push_back(m_seq.at(i));
    }
  }

  m_seq = tmp;
  m_extended = true;
}

void ebus::Sequence::reduce() {
  if (!m_extended) return;

  std::vector<uint8_t> tmp;
  bool extended = false;

  for (size_t i = 0; i < m_seq.size(); i++) {
    if (m_seq.at(i) == sym_syn || m_seq.at(i) == sym_exp) {
      extended = true;
    } else if (extended) {
      if (m_seq.at(i) == sym_synexp)
        tmp.push_back(sym_syn);
      else
        tmp.push_back(sym_exp);

      extended = false;
    } else {
      tmp.push_back(m_seq.at(i));
    }
  }

  m_seq = tmp;
  m_extended = false;
}

const std::string ebus::Sequence::to_string() const {
  std::ostringstream ostr;

  for (size_t i = 0; i < m_seq.size(); i++)
    ostr << std::nouppercase << std::hex << std::setw(2) << std::setfill('0')
         << static_cast<unsigned>(m_seq.at(i));

  return ostr.str();
}

const std::vector<uint8_t> &ebus::Sequence::to_vector() const { return m_seq; }

const std::vector<uint8_t> ebus::Sequence::range(
    const std::vector<uint8_t> &vec, const size_t index, const size_t len) {
  std::vector<uint8_t> result;

  for (size_t i = index; i < vec.size() && result.size() < len; i++)
    result.push_back(vec.at(i));

  return result;
}

const std::vector<uint8_t> ebus::Sequence::to_vector(const std::string &str) {
  std::vector<uint8_t> result;

  for (size_t i = 0; i + 1 < str.size(); i += 2)
    result.push_back(
        uint8_t(std::strtoul(str.substr(i, 2).c_str(), nullptr, 16)));

  return result;
}

const std::string ebus::Sequence::to_string(const std::vector<uint8_t> &vec) {
  std::ostringstream ostr;

  for (size_t i = 0; i < vec.size(); i++)
    ostr << std::nouppercase << std::hex << std::setw(2) << std::setfill('0')
         << static_cast<unsigned>(vec[i]);

  return ostr.str();
}

// CRC8 table of the polynom 0x9b = x^8 + x^7 + x^4 + x^3 + x^1 + 1.
static const uint8_t crc_table[] = {
    0x00, 0x9b, 0xad, 0x36, 0xc1, 0x5a, 0x6c, 0xf7, 0x19, 0x82, 0xb4, 0x2f,
    0xd8, 0x43, 0x75, 0xee, 0x32, 0xa9, 0x9f, 0x04, 0xf3, 0x68, 0x5e, 0xc5,
    0x2b, 0xb0, 0x86, 0x1d, 0xea, 0x71, 0x47, 0xdc, 0x64, 0xff, 0xc9, 0x52,
    0xa5, 0x3e, 0x08, 0x93, 0x7d, 0xe6, 0xd0, 0x4b, 0xbc, 0x27, 0x11, 0x8a,
    0x56, 0xcd, 0xfb, 0x60, 0x97, 0x0c, 0x3a, 0xa1, 0x4f, 0xd4, 0xe2, 0x79,
    0x8e, 0x15, 0x23, 0xb8, 0xc8, 0x53, 0x65, 0xfe, 0x09, 0x92, 0xa4, 0x3f,
    0xd1, 0x4a, 0x7c, 0xe7, 0x10, 0x8b, 0xbd, 0x26, 0xfa, 0x61, 0x57, 0xcc,
    0x3b, 0xa0, 0x96, 0x0d, 0xe3, 0x78, 0x4e, 0xd5, 0x22, 0xb9, 0x8f, 0x14,
    0xac, 0x37, 0x01, 0x9a, 0x6d, 0xf6, 0xc0, 0x5b, 0xb5, 0x2e, 0x18, 0x83,
    0x74, 0xef, 0xd9, 0x42, 0x9e, 0x05, 0x33, 0xa8, 0x5f, 0xc4, 0xf2, 0x69,
    0x87, 0x1c, 0x2a, 0xb1, 0x46, 0xdd, 0xeb, 0x70, 0x0b, 0x90, 0xa6, 0x3d,
    0xca, 0x51, 0x67, 0xfc, 0x12, 0x89, 0xbf, 0x24, 0xd3, 0x48, 0x7e, 0xe5,
    0x39, 0xa2, 0x94, 0x0f, 0xf8, 0x63, 0x55, 0xce, 0x20, 0xbb, 0x8d, 0x16,
    0xe1, 0x7a, 0x4c, 0xd7, 0x6f, 0xf4, 0xc2, 0x59, 0xae, 0x35, 0x03, 0x98,
    0x76, 0xed, 0xdb, 0x40, 0xb7, 0x2c, 0x1a, 0x81, 0x5d, 0xc6, 0xf0, 0x6b,
    0x9c, 0x07, 0x31, 0xaa, 0x44, 0xdf, 0xe9, 0x72, 0x85, 0x1e, 0x28, 0xb3,
    0xc3, 0x58, 0x6e, 0xf5, 0x02, 0x99, 0xaf, 0x34, 0xda, 0x41, 0x77, 0xec,
    0x1b, 0x80, 0xb6, 0x2d, 0xf1, 0x6a, 0x5c, 0xc7, 0x30, 0xab, 0x9d, 0x06,
    0xe8, 0x73, 0x45, 0xde, 0x29, 0xb2, 0x84, 0x1f, 0xa7, 0x3c, 0x0a, 0x91,
    0x66, 0xfd, 0xcb, 0x50, 0xbe, 0x25, 0x13, 0x88, 0x7f, 0xe4, 0xd2, 0x49,
    0x95, 0x0e, 0x38, 0xa3, 0x54, 0xcf, 0xf9, 0x62, 0x8c, 0x17, 0x21, 0xba,
    0x4d, 0xd6, 0xe0, 0x7b};

uint8_t ebus::Sequence::calc_crc(const uint8_t byte, const uint8_t init) {
  return uint8_t(crc_table[init] ^ byte);
}
