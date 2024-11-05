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

#include "Telegram.h"

#include <iomanip>
#include <map>
#include <sstream>

std::map<int, const char *> SequenceErrors =
	{

		{SEQ_EMPTY, "sequence is empty"},

		{SEQ_ERR_SHORT, "sequence is too short"},
		{SEQ_ERR_LONG, "sequence is too long"},
		{SEQ_ERR_NN, "number data byte is invalid"},
		{SEQ_ERR_CRC, "sequence has a CRC error"},
		{SEQ_ERR_ACK, "acknowledge byte is invalid"},
		{SEQ_ERR_QQ, "source address is invalid"},
		{SEQ_ERR_ZZ, "target address is invalid"},
		{SEQ_ERR_ACK_MISS, "acknowledge byte is missing"},
		{SEQ_ERR_INVALID, "sequence is invalid"}};

ebus::Telegram::Telegram(Sequence &seq)
{
	parse(seq);
}

void ebus::Telegram::parse(Sequence &seq)
{
	clear();
	seq.reduce();
	int offset = 0;

	m_masterState = checkMasterSequence(seq);

	if (m_masterState != SEQ_OK)
		return;

	Sequence master(seq, 0, 5 + uint8_t(seq[4]) + 1);
	createMaster(master);

	if (m_masterState != SEQ_OK)
		return;

	if (m_type != Type::BC)
	{
		// acknowledge byte is missing
		if (seq.size() <= (size_t)(5 + m_masterNN + 1))
		{
			m_slaveState = SEQ_ERR_ACK_MISS;
			return;
		}

		m_slaveACK = seq[5 + m_masterNN + 1];

		// acknowledge byte is invalid
		if (m_slaveACK != sym_ack && m_slaveACK != sym_nak)
		{
			m_slaveState = SEQ_ERR_ACK;
			return;
		}

		// handle NAK from slave
		if (m_slaveACK == sym_nak)
		{
			// sequence is too short
			if (seq.size() < (size_t)(master.size() + 1))
			{
				m_masterState = SEQ_ERR_SHORT;
				return;
			}

			offset = master.size() + 1;
			m_master.clear();

			Sequence tmp(seq, offset);
			m_masterState = checkMasterSequence(tmp);

			if (m_masterState != SEQ_OK)
				return;

			Sequence master2(tmp, 0, 5 + uint8_t(tmp[4]) + 1);
			createMaster(master2);

			if (m_masterState != SEQ_OK)
				return;

			// acknowledge byte is missing
			if (tmp.size() <= (size_t)(5 + m_masterNN + 1))
			{
				m_slaveState = SEQ_ERR_ACK_MISS;
				return;
			}

			m_slaveACK = tmp[5 + m_masterNN + 1];

			// acknowledge byte is invalid
			if (m_slaveACK != sym_ack && m_slaveACK != sym_nak)
			{
				m_slaveState = SEQ_ERR_ACK;
				return;
			}

			// acknowledge byte is negativ
			if (m_slaveACK == sym_nak)
			{
				// sequence is too long
				if (tmp.size() > (size_t)(5 + m_masterNN + 2))
					m_masterState = SEQ_ERR_LONG;

				// sequence is invalid
				else
					m_masterState = SEQ_ERR_INVALID;

				return;
			}
		}
	}

	if (m_type == Type::MS)
	{
		offset += 5 + m_masterNN + 2;

		Sequence seq2(seq, offset);
		m_slaveState = checkSlaveSequence(seq2);

		if (m_slaveState != SEQ_OK)
			return;

		Sequence slave(seq2, 0, 1 + uint8_t(seq2[0]) + 1);
		createSlave(slave);

		if (m_slaveState != SEQ_OK)
			return;

		// acknowledge byte is missing
		if (seq2.size() <= (size_t)(1 + m_slaveNN + 1))
		{
			m_masterState = SEQ_ERR_ACK_MISS;
			return;
		}

		m_masterACK = seq2[1 + m_slaveNN + 1];

		// acknowledge byte is invalid
		if (m_masterACK != sym_ack && m_masterACK != sym_nak)
		{
			m_masterState = SEQ_ERR_ACK;
			return;
		}

		// handle NAK from master
		if (m_masterACK == sym_nak)
		{
			// sequence is too short
			if (seq2.size() < (size_t)(slave.size() + 2))
			{
				m_slaveState = SEQ_ERR_SHORT;
				return;
			}

			offset = slave.size() + 2;
			m_slave.clear();

			Sequence tmp(seq2, offset);
			m_slaveState = checkSlaveSequence(tmp);

			if (m_slaveState != SEQ_OK)
				return;

			Sequence slave2(seq2, offset, 1 + uint8_t(seq2[offset]) + 1);
			createSlave(slave2);

			// acknowledge byte is missing
			if (tmp.size() <= (size_t)(1 + m_slaveNN + 1))
			{
				m_masterState = SEQ_ERR_ACK_MISS;
				return;
			}

			m_masterACK = tmp[1 + m_slaveNN + 1];

			// acknowledge byte is invalid
			if (m_masterACK != sym_ack && m_masterACK != sym_nak)
			{
				m_masterState = SEQ_ERR_ACK;
				return;
			}

			// sequence is too long
			if (tmp.size() > (size_t)(1 + m_slaveNN + 2))
			{
				m_slaveState = SEQ_ERR_LONG;
				m_slave.clear();
				return;
			}

			// acknowledge byte is negativ
			if (m_masterACK == sym_nak)
			{
				// sequence is invalid
				m_slaveState = SEQ_ERR_INVALID;
				return;
			}
		}
	}
}

void ebus::Telegram::createMaster(const uint8_t src, const std::vector<uint8_t> &vec)
{
	Sequence seq;

	seq.push_back(src, false);

	for (size_t i = 0; i < vec.size(); i++)
		seq.push_back(vec.at(i), false);

	createMaster(seq);
}

void ebus::Telegram::createMaster(Sequence &seq)
{
	m_masterState = SEQ_OK;
	seq.reduce();

	// sequence is too short
	if (seq.size() < 6)
	{
		m_masterState = SEQ_ERR_SHORT;
		return;
	}

	// source address is invalid
	if (!isMaster(seq[0]))
	{
		m_masterState = SEQ_ERR_QQ;
		return;
	}

	// target address is invalid
	if (!isAddressValid(seq[1]))
	{
		m_masterState = SEQ_ERR_ZZ;
		return;
	}

	// number data byte is invalid
	if (uint8_t(seq[4]) > max_bytes)
	{
		m_masterState = SEQ_ERR_NN;
		return;
	}

	// sequence is too short (excl. CRC)
	if (seq.size() < (size_t)(5 + uint8_t(seq[4])))
	{
		m_masterState = SEQ_ERR_SHORT;
		return;
	}

	// sequence is too long (incl. CRC)
	if (seq.size() > (size_t)(5 + uint8_t(seq[4]) + 1))
	{
		m_masterState = SEQ_ERR_LONG;
		return;
	}

	set_type(seq[1]);
	m_masterNN = (size_t)uint8_t(seq[4]);

	if (seq.size() == (size_t)(5 + m_masterNN))
	{
		m_master = seq;
		m_masterCRC = seq.crc();
	}
	else
	{
		m_master = Sequence(seq, 0, 5 + m_masterNN);
		m_masterCRC = seq[5 + m_masterNN];

		// sequence has a CRC error
		if (m_master.crc() != m_masterCRC)
			m_masterState = SEQ_ERR_CRC;
	}
}

void ebus::Telegram::createSlave(const std::vector<uint8_t> &vec)
{
	Sequence seq;

	for (size_t i = 0; i < vec.size(); i++)
		seq.push_back(vec.at(i), false);

	createSlave(seq);
}

void ebus::Telegram::createSlave(Sequence &seq)
{
	m_slaveState = SEQ_OK;
	seq.reduce();

	// sequence is too short
	if (seq.size() < (size_t)2)
	{
		m_slaveState = SEQ_ERR_SHORT;
		return;
	}

	// number data byte is invalid
	if (uint8_t(seq[0]) > max_bytes)
	{
		m_slaveState = SEQ_ERR_NN;
		return;
	}

	// sequence is too short (excl. CRC)
	if (seq.size() < (size_t)(1 + uint8_t(seq[0])))
	{
		m_slaveState = SEQ_ERR_SHORT;
		return;
	}

	// sequence is too long (incl. CRC)
	if (seq.size() > (size_t)(1 + uint8_t(seq[0]) + 1))
	{
		m_slaveState = SEQ_ERR_LONG;
		return;
	}

	m_slaveNN = (size_t)uint8_t(seq[0]);

	if (seq.size() == (1 + m_slaveNN))
	{
		m_slave = seq;
		m_slaveCRC = seq.crc();
	}
	else
	{
		m_slave = Sequence(seq, 0, 1 + m_slaveNN);
		m_slaveCRC = seq[1 + m_slaveNN];

		// sequence has a CRC error
		if (m_slave.crc() != m_slaveCRC)
			m_slaveState = SEQ_ERR_CRC;
	}
}

void ebus::Telegram::clear()
{
	m_type = Type::undefined;

	m_master.clear();
	m_masterNN = 0;
	m_masterCRC = sym_zero;
	m_masterACK = sym_zero;
	m_masterState = SEQ_EMPTY;

	m_slave.clear();
	m_slaveNN = 0;
	m_slaveCRC = sym_zero;
	m_slaveACK = sym_zero;
	m_slaveState = SEQ_EMPTY;
}

uint8_t ebus::Telegram::getSourceAddress() const
{
	return (m_master[0]);
}

uint8_t ebus::Telegram::getTargetAddress() const
{
	return (m_master[1]);
}

ebus::Sequence ebus::Telegram::getMaster() const
{
	return (m_master);
}

uint8_t ebus::Telegram::getMasterCRC() const
{
	return (m_masterCRC);
}

int ebus::Telegram::getMasterState() const
{
	return (m_masterState);
}

void ebus::Telegram::setSlaveACK(const uint8_t byte)
{
	m_slaveACK = byte;
}

ebus::Sequence ebus::Telegram::getSlave() const
{
	return (m_slave);
}

uint8_t ebus::Telegram::getSlaveCRC() const
{
	return (m_slaveCRC);
}

int ebus::Telegram::getSlaveState() const
{
	return (m_slaveState);
}

void ebus::Telegram::setMasterACK(const uint8_t byte)
{
	m_masterACK = byte;
}

ebus::Type ebus::Telegram::get_type() const
{
	return (m_type);
}

bool ebus::Telegram::isValid() const
{
	if (m_type != Type::MS)
		return (m_masterState == SEQ_OK ? true : false);

	return ((m_masterState + m_slaveState) == SEQ_OK ? true : false);
}

const std::string ebus::Telegram::to_string()
{
	std::ostringstream ostr;

	ostr << toStringMaster();

	if (m_masterState == SEQ_OK && m_type == Type::MS)
		ostr << " " << toStringSlave();

	return (ostr.str());
}

const std::string ebus::Telegram::toStringMaster()
{
	std::ostringstream ostr;
	if (m_masterState != SEQ_OK)
		ostr << toStringMasterError();
	else
		ostr << m_master.to_string();

	return (ostr.str());
}

const std::string ebus::Telegram::toStringSlave()
{
	std::ostringstream ostr;
	if (m_slaveState != SEQ_OK && m_type != Type::BC)
	{
		ostr << toStringSlaveError();
	}
	else
	{
		if (m_type == Type::MS)
			ostr << m_slave.to_string();
	}

	return (ostr.str());
}

bool ebus::Telegram::isMaster(const uint8_t byte)
{
	uint8_t hi = (byte & uint8_t(0xf0)) >> 4;
	uint8_t lo = (byte & uint8_t(0x0f));

	return (((hi == uint8_t(0x0)) || (hi == uint8_t(0x1)) || (hi == uint8_t(0x3)) || (hi == uint8_t(0x7)) || (hi == uint8_t(0xf))) && ((lo == uint8_t(0x0)) || (lo == uint8_t(0x1)) || (lo == uint8_t(0x3)) || (lo == uint8_t(0x7)) || (lo == uint8_t(0xf))));
}

bool ebus::Telegram::isSlave(const uint8_t byte)
{
	return (!isMaster(byte) && byte != sym_syn && byte != sym_exp);
}

uint8_t ebus::Telegram::slaveAddress(const uint8_t address)
{
	if (isSlave(address))
		return (address);

	return (uint8_t(address + 5));
}

const std::string ebus::Telegram::errorText(const int error)
{
	std::ostringstream ostr;

	ostr << SequenceErrors[error];

	return (ostr.str());
}

const std::string ebus::Telegram::toStringMasterError()
{
	std::ostringstream ostr;
	if (m_master.size() > 0)
		ostr << "'" << m_master.to_string() << "' ";

	ostr << "master " << errorText(m_masterState);

	return (ostr.str());
}

const std::string ebus::Telegram::toStringSlaveError()
{
	std::ostringstream ostr;
	if (m_slave.size() > 0)
		ostr << "'" << m_slave.to_string() << "' ";

	ostr << "slave " << errorText(m_slaveState);

	return (ostr.str());
}

void ebus::Telegram::set_type(const uint8_t byte)
{
	if (byte == sym_broad)
		m_type = Type::BC;
	else if (isMaster(byte))
		m_type = Type::MM;
	else
		m_type = Type::MS;
}

bool ebus::Telegram::isAddressValid(const uint8_t byte)
{
	return (byte != sym_syn && byte != sym_exp);
}

int ebus::Telegram::checkMasterSequence(Sequence &seq)
{
	// sequence is too short
	if (seq.size() < (size_t)6)
		return (SEQ_ERR_SHORT);

	// source address is invalid
	if (!isMaster(seq[0]))
		return (SEQ_ERR_QQ);

	// target address is invalid
	if (!isAddressValid(seq[1]))
		return (SEQ_ERR_ZZ);

	// number data byte is invalid
	if (uint8_t(seq[4]) > max_bytes)
		return (SEQ_ERR_NN);

	// sequence is too short (incl. CRC)
	if (seq.size() < (size_t)(5 + uint8_t(seq[4]) + 1))
		return (SEQ_ERR_SHORT);

	return (SEQ_OK);
}

int ebus::Telegram::checkSlaveSequence(Sequence &seq)
{
	// sequence is too short
	if (seq.size() < (size_t)2)
		return (SEQ_ERR_SHORT);

	// number data byte is invalid
	if (uint8_t(seq[0]) > max_bytes)
		return (SEQ_ERR_NN);

	// sequence is too short (incl. CRC)
	if (seq.size() < (size_t)(1 + uint8_t(seq[0]) + 1))
		return (SEQ_ERR_SHORT);

	return (SEQ_OK);
}
