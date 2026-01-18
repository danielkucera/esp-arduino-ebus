#if defined(EBUS_INTERNAL)
#include "ClientType.hpp"

enum requests { CMD_INIT = 0, CMD_SEND, CMD_START, CMD_INFO };

enum responses {
  RESETTED = 0x0,
  RECEIVED = 0x1,
  STARTED = 0x2,
  INFO = 0x3,
  FAILED = 0xa,
  ERROR_EBUS = 0xb,
  ERROR_HOST = 0xc
};

enum errors { ERR_FRAMING = 0x00, ERR_OVERRUN = 0x01 };

AbstractClient::AbstractClient(WiFiClient* client, ebus::Request* request,
                               bool write)
    : client(client), request(request), write(write) {}

bool AbstractClient::isWriteCapable() const { return write; }

bool AbstractClient::isConnected() const {
  return client && client->connected();
}

void AbstractClient::stop() {
  if (client) client->stop();
}

ReadOnlyClient::ReadOnlyClient(WiFiClient* client, ebus::Request* request)
    : AbstractClient(client, request, false) {}

bool ReadOnlyClient::available() const { return false; }

bool ReadOnlyClient::readByte(uint8_t& byte) { return false; }

bool ReadOnlyClient::writeBytes(const std::vector<uint8_t>& bytes) {
  if (!isConnected() || bytes.empty()) return false;

  client->write(bytes.data(), bytes.size());
  return true;
}

bool ReadOnlyClient::handleBusData(const uint8_t& byte) { return false; }

RegularClient::RegularClient(WiFiClient* client, ebus::Request* request)
    : AbstractClient(client, request, true) {}

bool RegularClient::available() const { return client && client->available(); }

bool RegularClient::readByte(uint8_t& byte) {
  if (available()) {
    byte = client->read();
    return true;
  }
  return false;
}

bool RegularClient::writeBytes(const std::vector<uint8_t>& bytes) {
  if (!isConnected() || bytes.empty()) return false;

  client->write(bytes.data(), bytes.size());
  return true;
}

bool RegularClient::handleBusData(const uint8_t& byte) {
  // Handle bus response according to last command
  switch (request->getResult()) {
    case ebus::RequestResult::observeSyn:
    case ebus::RequestResult::firstLost:
    case ebus::RequestResult::firstError:
    case ebus::RequestResult::retryError:
    case ebus::RequestResult::secondLost:
    case ebus::RequestResult::secondError:
      return false;
    case ebus::RequestResult::observeData:
    case ebus::RequestResult::firstSyn:
    case ebus::RequestResult::firstRetry:
    case ebus::RequestResult::retrySyn:
    case ebus::RequestResult::firstWon:
    case ebus::RequestResult::secondWon:
      writeBytes({byte});
      return true;
    default:
      break;
  }
  return false;
}

EnhancedClient::EnhancedClient(WiFiClient* client, ebus::Request* request)
    : AbstractClient(client, request, true) {}

bool EnhancedClient::available() const { return client && client->available(); }

bool EnhancedClient::readByte(uint8_t& byte) {
  int b1 = client->peek();
  if (b1 < 0) return false;

  if (b1 < 0x80) {
    // Short form: just a data byte, no prefix
    byte = client->read();
    return true;
  }

  // Full enhanced protocol: need two bytes
  if (client->available() < 2) return false;
  b1 = client->read();
  int b2 = client->read();

  // Check signatures
  if ((b1 & 0xc0) != 0xc0 || (b2 & 0xc0) != 0x80) {
    // Invalid signature, protocol error
    writeBytes({ERROR_HOST, ERR_FRAMING});
    client->stop();
    return false;
  }

  // Decode command and data according to enhanced protocol
  uint8_t cmd = (b1 >> 2) & 0x0f;
  uint8_t data = ((b1 & 0x03) << 6) | (b2 & 0x3f);

  // Handle commands
  switch (cmd) {
    case CMD_INIT:
      writeBytes({RESETTED, 0x0});
      return false;
    case CMD_SEND:
      byte = data;
      return true;
    case CMD_START:
      if (data == ebus::sym_syn) return false;
      byte = data;
      return true;
    case CMD_INFO:
      return false;
    default:
      break;
  }

  return false;
}

bool EnhancedClient::writeBytes(const std::vector<uint8_t>& bytes) {
  if (!isConnected() || bytes.empty()) return false;

  uint8_t cmd = RECEIVED;
  uint8_t data = bytes[0];

  if (bytes.size() == 2) {
    cmd = bytes[0];
    data = bytes[1];
  }

  // Short form for data < 0x80
  if (bytes.size() == 1 && data < 0x80) {
    client->write(data);
  } else {
    uint8_t out[2];
    out[0] = 0xc0 | (cmd << 2) | (data >> 6);
    out[1] = 0x80 | (data & 0x3f);
    client->write(out, 2);
  }
  return true;
}

bool EnhancedClient::handleBusData(const uint8_t& byte) {
  // Handle bus response according to last command
  switch (request->getResult()) {
    case ebus::RequestResult::observeSyn:
    case ebus::RequestResult::firstLost:
    case ebus::RequestResult::secondLost:
      writeBytes({FAILED, byte});
      return false;
    case ebus::RequestResult::firstError:
    case ebus::RequestResult::retryError:
    case ebus::RequestResult::secondError:
      writeBytes({ERROR_EBUS, ERR_FRAMING});
      return false;
    case ebus::RequestResult::observeData:
      writeBytes({RECEIVED, byte});
      return true;
    case ebus::RequestResult::firstSyn:
    case ebus::RequestResult::firstRetry:
    case ebus::RequestResult::retrySyn:
      // Waiting for arbitration, do nothing
      return true;
    case ebus::RequestResult::firstWon:
    case ebus::RequestResult::secondWon:
      writeBytes({STARTED, byte});
      return true;
    default:
      break;
  }
  return false;
}

#endif