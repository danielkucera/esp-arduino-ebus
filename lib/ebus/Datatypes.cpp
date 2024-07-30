/*
 * Copyright (C) Roland Jax 2017-2019 <roland.jax@liwest.at>
 *
 * This file is part of ebustrace.
 *
 * ebustrace is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ebustrace is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ebustrace. If not, see http://www.gnu.org/licenses/.
 */

#include "Datatypes.h"

// helper functions
uint convert_base(uint value, const uint& oldBase, const uint& newBase)
{
	uint result = 0;
	for (uint i = 0; value > 0; i++)
	{
		result += value % oldBase * pow(newBase, i);
		value /= oldBase;
	}
	return (result);
}

double_t round_digits(const double_t& value, const uint8_t& digits)
{
	double_t fractpart, intpart;
	fractpart = modf(value, &intpart);

	double_t decimals = pow(10, digits);

	return (static_cast<double_t>(intpart) + round(fractpart * decimals) / decimals);
}

// BCD
uint8_t byte_2_bcd(const std::vector<uint8_t>& bytes)
{
	uint8_t value = bytes[0];
	uint8_t result = convert_base(value, 16, 10);

	if ((value & 0x0f) > 9 || ((value >> 4) & 0x0f) > 9) result = 0xff;

	return (result);
}

std::vector<uint8_t> bcd_2_byte(const uint8_t& value)
{
	uint8_t byte = convert_base(value, 10, 16);
	std::vector<uint8_t> result(1, uint8_t(byte));

	if (value > 99) result[0] = uint8_t(0xff);

	return (result);
}


// uint8_t
uint8_t byte_2_uint8(const std::vector<uint8_t>& bytes)
{
	return (byte2int<uint8_t>(bytes));
}

std::vector<uint8_t> uint8_2_byte(const uint8_t& value)
{
	return (int2byte<uint8_t>(value));
}


// int8_t
int8_t byte_2_int8(const std::vector<uint8_t>& bytes)
{
	return (byte2int<int8_t>(bytes));
}

std::vector<uint8_t> int8_2_byte(const int8_t& value)
{
	return (int2byte<int8_t>(value));
}


// uint16_t
uint16_t byte_2_uint16(const std::vector<uint8_t>& bytes)
{
	return (byte2int<uint16_t>(bytes));
}

std::vector<uint8_t> uint16_2_byte(const uint16_t& value)
{
	return (int2byte<uint16_t>(value));
}


// int16_t
int16_t byte_2_int16(const std::vector<uint8_t>& bytes)
{
	return (byte2int<int16_t>(bytes));
}

std::vector<uint8_t> int16_2_byte(const int16_t& value)
{
	return (int2byte<int16_t>(value));
}


// DATA1b
double_t byte_2_data1b(const std::vector<uint8_t>& bytes)
{
	return (static_cast<double_t>(byte2int<int8_t>(bytes)));
}

std::vector<uint8_t> data1b_2_byte(const double_t& value)
{
	return (int2byte(static_cast<int8_t>(value)));
}


// DATA1c
double_t byte_2_data1c(const std::vector<uint8_t>& bytes)
{
	return (static_cast<double_t>(byte2int<uint8_t>(bytes)) / 2);
}

std::vector<uint8_t> data1c_2_byte(const double_t& value)
{
	return (int2byte(static_cast<uint8_t>(value * 2)));
}


// DATA2b
double_t byte_2_data2b(const std::vector<uint8_t>& bytes)
{
	return (static_cast<double_t>(byte2int<int16_t>(bytes)) / 256);
}

std::vector<uint8_t> data2b_2_byte(const double_t& value)
{
	return (int2byte(static_cast<int16_t>(value * 256)));
}


// DATA2c
double_t byte_2_data2c(const std::vector<uint8_t>& bytes)
{
	return (static_cast<double_t>(byte2int<int16_t>(bytes)) / 16);
}

std::vector<uint8_t> data2c_2_byte(const double_t& value)
{
	return (int2byte(static_cast<int16_t>(value * 16)));
}


// float
double_t byte_2_float(const std::vector<uint8_t>& bytes)
{
	return (round_digits(static_cast<double_t>(byte2int<int16_t>(bytes)) / 1000, 3));
}

std::vector<uint8_t> float_2_byte(const double_t& value)
{
	return (int2byte(static_cast<int16_t>(round_digits(value * 1000, 3))));
}

