/*
 * Copyright (C) Roland Jax 2012-2022 <roland.jax@liwest.at>
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

ebus::Sequence::Sequence(const Sequence &seq, const size_t index, size_t len)
{
	if (len == 0) len = seq.size() - index;

	for (size_t i = index; i < index + len; i++)
		m_seq.push_back(seq.m_seq.at(i));

	m_extended = seq.m_extended;
}

void ebus::Sequence::assign(const std::vector<uint8_t> &vec, const bool extended)
{
	clear();

	for (size_t i = 0; i < vec.size(); i++)
		push_back(vec[i], extended);
}

void ebus::Sequence::push_back(const uint8_t byte, const bool extended)
{
	m_seq.push_back(byte);
	m_extended = extended;
}

const uint8_t& ebus::Sequence::operator[](const size_t index) const
{
	return (m_seq.at(index));
}

const std::vector<uint8_t> ebus::Sequence::range(const size_t index, const size_t len)
{
	return (range(m_seq, index, len));
}

size_t ebus::Sequence::size() const
{
	return (m_seq.size());
}

void ebus::Sequence::clear()
{
	m_seq.clear();
	m_seq.shrink_to_fit();
	m_extended = false;
}

uint8_t ebus::Sequence::crc()
{
	if (!m_extended) extend();

	uint8_t crc = seq_zero;

	for (size_t i = 0; i < m_seq.size(); i++)
		crc = calc_crc(m_seq.at(i), crc);

	if (!m_extended) reduce();

	return (crc);
}

void ebus::Sequence::extend()
{
	if (m_extended) return;

	std::vector<uint8_t> tmp;

	for (size_t i = 0; i < m_seq.size(); i++)
	{
		if (m_seq.at(i) == seq_syn)
		{
			tmp.push_back(seq_exp);
			tmp.push_back(seq_synexp);
		}
		else if (m_seq.at(i) == seq_exp)
		{
			tmp.push_back(seq_exp);
			tmp.push_back(seq_expexp);
		}
		else
		{
			tmp.push_back(m_seq.at(i));
		}
	}

	m_seq = tmp;
	m_extended = true;
}

void ebus::Sequence::reduce()
{
	if (!m_extended) return;

	std::vector<uint8_t> tmp;
	bool extended = false;

	for (size_t i = 0; i < m_seq.size(); i++)
	{
		if (m_seq.at(i) == seq_syn || m_seq.at(i) == seq_exp)
		{
			extended = true;
		}
		else if (extended)
		{
			if (m_seq.at(i) == seq_synexp)
				tmp.push_back(seq_syn);
			else
				tmp.push_back(seq_exp);

			extended = false;
		}
		else
		{
			tmp.push_back(m_seq.at(i));
		}
	}

	m_seq = tmp;
	m_extended = false;
}

const std::string ebus::Sequence::to_string() const
{
	std::ostringstream ostr;

	for (size_t i = 0; i < m_seq.size(); i++)
		ostr << std::nouppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(m_seq.at(i));

	return (ostr.str());
}

const std::vector<uint8_t> ebus::Sequence::get_sequence() const
{
	return (m_seq);
}

const std::vector<uint8_t> ebus::Sequence::range(const std::vector<uint8_t> &seq, const size_t index, const size_t len)
{
	std::vector<uint8_t> result;

	for (size_t i = index; i < seq.size() && result.size() < len; i++)
		result.push_back(seq.at(i));

	return (result);
}

// CRC8 table of the polynom 0x9b = x^8 + x^7 + x^4 + x^3 + x^1 + 1.
// @formatter:off
static const uint8_t crc_table[] = {
 uint8_t(0x00), uint8_t(0x9b), uint8_t(0xad), uint8_t(0x36), uint8_t(0xc1), uint8_t(0x5a), uint8_t(0x6c), uint8_t(0xf7),
 uint8_t(0x19), uint8_t(0x82), uint8_t(0xb4), uint8_t(0x2f), uint8_t(0xd8), uint8_t(0x43), uint8_t(0x75), uint8_t(0xee),
 uint8_t(0x32), uint8_t(0xa9), uint8_t(0x9f), uint8_t(0x04), uint8_t(0xf3), uint8_t(0x68), uint8_t(0x5e), uint8_t(0xc5),
 uint8_t(0x2b), uint8_t(0xb0), uint8_t(0x86), uint8_t(0x1d), uint8_t(0xea), uint8_t(0x71), uint8_t(0x47), uint8_t(0xdc),
 uint8_t(0x64), uint8_t(0xff), uint8_t(0xc9), uint8_t(0x52), uint8_t(0xa5), uint8_t(0x3e), uint8_t(0x08), uint8_t(0x93),
 uint8_t(0x7d), uint8_t(0xe6), uint8_t(0xd0), uint8_t(0x4b), uint8_t(0xbc), uint8_t(0x27), uint8_t(0x11), uint8_t(0x8a),
 uint8_t(0x56), uint8_t(0xcd), uint8_t(0xfb), uint8_t(0x60), uint8_t(0x97), uint8_t(0x0c), uint8_t(0x3a), uint8_t(0xa1),
 uint8_t(0x4f), uint8_t(0xd4), uint8_t(0xe2), uint8_t(0x79), uint8_t(0x8e), uint8_t(0x15), uint8_t(0x23), uint8_t(0xb8),
 uint8_t(0xc8), uint8_t(0x53), uint8_t(0x65), uint8_t(0xfe), uint8_t(0x09), uint8_t(0x92), uint8_t(0xa4), uint8_t(0x3f),
 uint8_t(0xd1), uint8_t(0x4a), uint8_t(0x7c), uint8_t(0xe7), uint8_t(0x10), uint8_t(0x8b), uint8_t(0xbd), uint8_t(0x26),
 uint8_t(0xfa), uint8_t(0x61), uint8_t(0x57), uint8_t(0xcc), uint8_t(0x3b), uint8_t(0xa0), uint8_t(0x96), uint8_t(0x0d),
 uint8_t(0xe3), uint8_t(0x78), uint8_t(0x4e), uint8_t(0xd5), uint8_t(0x22), uint8_t(0xb9), uint8_t(0x8f), uint8_t(0x14),
 uint8_t(0xac), uint8_t(0x37), uint8_t(0x01), uint8_t(0x9a), uint8_t(0x6d), uint8_t(0xf6), uint8_t(0xc0), uint8_t(0x5b),
 uint8_t(0xb5), uint8_t(0x2e), uint8_t(0x18), uint8_t(0x83), uint8_t(0x74), uint8_t(0xef), uint8_t(0xd9), uint8_t(0x42),
 uint8_t(0x9e), uint8_t(0x05), uint8_t(0x33), uint8_t(0xa8), uint8_t(0x5f), uint8_t(0xc4), uint8_t(0xf2), uint8_t(0x69),
 uint8_t(0x87), uint8_t(0x1c), uint8_t(0x2a), uint8_t(0xb1), uint8_t(0x46), uint8_t(0xdd), uint8_t(0xeb), uint8_t(0x70),
 uint8_t(0x0b), uint8_t(0x90), uint8_t(0xa6), uint8_t(0x3d), uint8_t(0xca), uint8_t(0x51), uint8_t(0x67), uint8_t(0xfc),
 uint8_t(0x12), uint8_t(0x89), uint8_t(0xbf), uint8_t(0x24), uint8_t(0xd3), uint8_t(0x48), uint8_t(0x7e), uint8_t(0xe5),
 uint8_t(0x39), uint8_t(0xa2), uint8_t(0x94), uint8_t(0x0f), uint8_t(0xf8), uint8_t(0x63), uint8_t(0x55), uint8_t(0xce),
 uint8_t(0x20), uint8_t(0xbb), uint8_t(0x8d), uint8_t(0x16), uint8_t(0xe1), uint8_t(0x7a), uint8_t(0x4c), uint8_t(0xd7),
 uint8_t(0x6f), uint8_t(0xf4), uint8_t(0xc2), uint8_t(0x59), uint8_t(0xae), uint8_t(0x35), uint8_t(0x03), uint8_t(0x98),
 uint8_t(0x76), uint8_t(0xed), uint8_t(0xdb), uint8_t(0x40), uint8_t(0xb7), uint8_t(0x2c), uint8_t(0x1a), uint8_t(0x81),
 uint8_t(0x5d), uint8_t(0xc6), uint8_t(0xf0), uint8_t(0x6b), uint8_t(0x9c), uint8_t(0x07), uint8_t(0x31), uint8_t(0xaa),
 uint8_t(0x44), uint8_t(0xdf), uint8_t(0xe9), uint8_t(0x72), uint8_t(0x85), uint8_t(0x1e), uint8_t(0x28), uint8_t(0xb3),
 uint8_t(0xc3), uint8_t(0x58), uint8_t(0x6e), uint8_t(0xf5), uint8_t(0x02), uint8_t(0x99), uint8_t(0xaf), uint8_t(0x34),
 uint8_t(0xda), uint8_t(0x41), uint8_t(0x77), uint8_t(0xec), uint8_t(0x1b), uint8_t(0x80), uint8_t(0xb6), uint8_t(0x2d),
 uint8_t(0xf1), uint8_t(0x6a), uint8_t(0x5c), uint8_t(0xc7), uint8_t(0x30), uint8_t(0xab), uint8_t(0x9d), uint8_t(0x06),
 uint8_t(0xe8), uint8_t(0x73), uint8_t(0x45), uint8_t(0xde), uint8_t(0x29), uint8_t(0xb2), uint8_t(0x84), uint8_t(0x1f),
 uint8_t(0xa7), uint8_t(0x3c), uint8_t(0x0a), uint8_t(0x91), uint8_t(0x66), uint8_t(0xfd), uint8_t(0xcb), uint8_t(0x50),
 uint8_t(0xbe), uint8_t(0x25), uint8_t(0x13), uint8_t(0x88), uint8_t(0x7f), uint8_t(0xe4), uint8_t(0xd2), uint8_t(0x49),
 uint8_t(0x95), uint8_t(0x0e), uint8_t(0x38), uint8_t(0xa3), uint8_t(0x54), uint8_t(0xcf), uint8_t(0xf9), uint8_t(0x62),
 uint8_t(0x8c), uint8_t(0x17), uint8_t(0x21), uint8_t(0xba), uint8_t(0x4d), uint8_t(0xd6), uint8_t(0xe0), uint8_t(0x7b)};
// @formatter:on

uint8_t ebus::Sequence::calc_crc(const uint8_t byte, const uint8_t init)
{
	return (uint8_t(crc_table[init] ^ byte));
}
