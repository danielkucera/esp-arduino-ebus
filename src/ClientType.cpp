#if defined(EBUS_INTERNAL)
/*
#include "ClientType.hpp"

#include <cerrno>

#include <lwip/sockets.h>
#include <unistd.h>

namespace {

bool isSocketConnected(int socketFd) {
  if (socketFd < 0) return false;
  char buffer = 0;
  const int result = recv(socketFd, &buffer, 1, MSG_PEEK | MSG_DONTWAIT);
  if (result > 0) return true;
  if (result == 0) return false;
  return errno == EWOULDBLOCK || errno == EAGAIN;
}

int socketPeekByte(int socketFd) {
  if (socketFd < 0) return -1;
  uint8_t byte = 0;
  const int result = recv(socketFd, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
  if (result <= 0) return -1;
  return byte;
}

int socketPeekBytes(int socketFd, size_t maxSize) {
  if (socketFd < 0 || maxSize == 0) return 0;
  uint8_t buffer[2];
  const int result = recv(socketFd, buffer, maxSize, MSG_PEEK | MSG_DONTWAIT);
  if (result <= 0) return 0;
  return result;
}

int socketAvailable(int socketFd) {
  if (socketFd < 0) return 0;
  int pending = 0;
  if (lwip_ioctl(socketFd, FIONREAD, &pending) == 0 && pending > 0) {
    return pending;
  }
  // Fallback: use peek to check if data is available
  // Need to check for up to 2 bytes for enhanced protocol support
  const int peekResult = socketPeekBytes(socketFd, 2);
  return peekResult;
}

int socketReadByte(int socketFd) {
  if (socketFd < 0) return -1;
  uint8_t byte = 0;
  const int result = recv(socketFd, &byte, 1, MSG_DONTWAIT);
  if (result <= 0) return -1;
  return byte;
}

void closeSocket(int& socketFd) {
  if (socketFd >= 0) {
    shutdown(socketFd, SHUT_RDWR);
    close(socketFd);
    socketFd = -1;
  }
}

bool socketWrite(int socketFd, const uint8_t* data, size_t size) {
  if (socketFd < 0 || data == nullptr || size == 0) return false;
  const int result = send(socketFd, data, size, 0);
  return result > 0;
}

}  // namespace

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

AbstractClient::AbstractClient(int socketFd, ebus::Request* request, bool write)
  : socketFd(socketFd), request(request), write(write) {}

bool AbstractClient::isWriteCapable() const { return write; }

bool AbstractClient::isConnected() const { return isSocketConnected(socketFd); }

void AbstractClient::stop() {
  closeSocket(socketFd);
}

ReadOnlyClient::ReadOnlyClient(int socketFd, ebus::Request* request)
    : AbstractClient(socketFd, request, false) {}

bool ReadOnlyClient::available() const { return false; }

bool ReadOnlyClient::readByte(uint8_t& byte) { return false; }

bool ReadOnlyClient::writeBytes(const std::vector<uint8_t>& bytes) {
  if (!isConnected() || bytes.empty()) return false;

  socketWrite(socketFd, bytes.data(), bytes.size());
  return true;
}

bool ReadOnlyClient::handleBusData(const uint8_t& byte) { return false; }

RegularClient::RegularClient(int socketFd, ebus::Request* request)
    : AbstractClient(socketFd, request, true) {}

bool RegularClient::available() const { return socketAvailable(socketFd) > 0; }

bool RegularClient::readByte(uint8_t& byte) {
  if (available()) {
    const int value = socketReadByte(socketFd);
    if (value >= 0) {
      byte = static_cast<uint8_t>(value);
      return true;
    }
  }
  return false;
}

bool RegularClient::writeBytes(const std::vector<uint8_t>& bytes) {
  if (!isConnected() || bytes.empty()) return false;

  socketWrite(socketFd, bytes.data(), bytes.size());
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

EnhancedClient::EnhancedClient(int socketFd, ebus::Request* request)
    : AbstractClient(socketFd, request, true) {}

bool EnhancedClient::available() const { return socketAvailable(socketFd) > 0; }

bool EnhancedClient::readByte(uint8_t& byte) {
  int b1 = socketPeekByte(socketFd);
  if (b1 < 0) return false;

  if (b1 < 0x80) {
    // Short form: just a data byte, no prefix
    const int value = socketReadByte(socketFd);
    if (value < 0) return false;
    byte = static_cast<uint8_t>(value);
    return true;
  }

  // Full enhanced protocol: need two bytes
  if (socketAvailable(socketFd) < 2) return false;
  b1 = socketReadByte(socketFd);
  int b2 = socketReadByte(socketFd);

  // Check signatures
  if ((b1 & 0xc0) != 0xc0 || (b2 & 0xc0) != 0x80) {
    // Invalid signature, protocol error
    writeBytes({ERROR_HOST, ERR_FRAMING});
    stop();
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
    socketWrite(socketFd, &data, 1);
  } else {
    uint8_t out[2];
    out[0] = 0xc0 | (cmd << 2) | (data >> 6);
    out[1] = 0x80 | (data & 0x3f);
    socketWrite(socketFd, out, 2);
  }
  return true;
}

bool EnhancedClient::handleBusData(const uint8_t& byte) {
  // Handle bus response according to last command
  switch (request->getResult()) {
    case ebus::RequestResult::observeSyn:
      if (byte == ebus::sym_syn) {
        // Sync byte observed, send ACK
        writeBytes({RECEIVED, byte});
        return false;
      }
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
*/
#endif