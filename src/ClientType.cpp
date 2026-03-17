#if defined(EBUS_INTERNAL)
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

AbstractClient::AbstractClient(int socketFd, ebus::Bus* bus,
                               ebus::Request* request, bool write)
    : socketFd(socketFd), bus(bus), request(request), write(write) {
  connected = isSocketConnected(socketFd);
  rxQueue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(uint8_t));
  xTaskCreate(&AbstractClient::readerTaskEntry, "client_rx", 3072, this, 2,
              &readerTask);
}

AbstractClient::~AbstractClient() { stop(); }

bool AbstractClient::isWriteCapable() const { return write; }

bool AbstractClient::isConnected() const { return connected; }

void AbstractClient::stop() {
  stopReader = true;
  connected = false;

  if (socketFd >= 0) {
    shutdown(socketFd, SHUT_RDWR);
  }

  if (readerTask != nullptr) {
    vTaskDelete(readerTask);
    readerTask = nullptr;
  }

  closeSocket(socketFd);

  if (rxQueue != nullptr) {
    vQueueDelete(rxQueue);
    rxQueue = nullptr;
  }
}

bool AbstractClient::popRxByte(uint8_t& byte) {
  if (rxQueue == nullptr) return false;
  return xQueueReceive(rxQueue, &byte, 0) == pdTRUE;
}

bool AbstractClient::peekRxByte(uint8_t& byte) {
  if (rxQueue == nullptr) return false;
  return xQueuePeek(rxQueue, &byte, 0) == pdTRUE;
}

size_t AbstractClient::availableRx() const {
  if (rxQueue == nullptr) return 0;
  return static_cast<size_t>(uxQueueMessagesWaiting(rxQueue));
}

void AbstractClient::readerTaskEntry(void* arg) {
  auto* self = static_cast<AbstractClient*>(arg);
  self->readerTaskLoop();
}

void AbstractClient::readerTaskLoop() {
  while (!stopReader) {
    if (socketFd < 0 || rxQueue == nullptr) {
      connected = false;
      vTaskDelay(1);
      continue;
    }

    uint8_t byte = 0;
    int result = recv(socketFd, &byte, 1, 0);  // blocking read
    if (result > 0) {
      connected = true;
      xQueueSend(rxQueue, &byte, 0);
      continue;
    }

    if (result == 0) {
      connected = false;
      break;
    }

    if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) {
      continue;
    }

    connected = false;
    break;
  }
}

ReadOnlyClient::ReadOnlyClient(int socketFd, ebus::Bus* bus,
                               ebus::Request* request)
    : AbstractClient(socketFd, bus, request, false) {}

bool ReadOnlyClient::available() const { return false; }

bool ReadOnlyClient::readByte(uint8_t& byte) { return false; }

bool ReadOnlyClient::writeBytes(const std::vector<uint8_t>& bytes) {
  if (!isConnected() || bytes.empty()) return false;

  socketWrite(socketFd, bytes.data(), bytes.size());
  return true;
}

RegularClient::RegularClient(int socketFd, ebus::Bus* bus,
                             ebus::Request* request)
    : AbstractClient(socketFd, bus, request, true) {}

bool RegularClient::available() const { return availableRx() > 0; }

bool RegularClient::readByte(uint8_t& byte) {
  return popRxByte(byte);
}

bool RegularClient::writeBytes(const std::vector<uint8_t>& bytes) {
  if (!isConnected() || bytes.empty()) return false;

  socketWrite(socketFd, bytes.data(), bytes.size());
  return true;
}

EnhancedClient::EnhancedClient(int socketFd, ebus::Bus* bus,
                               ebus::Request* request)
    : AbstractClient(socketFd, bus, request, true) {}

bool EnhancedClient::available() const {
  return pendingRawByte >= 0 || availableRx() > 0;
}

bool EnhancedClient::readByte(uint8_t& byte) {
  int b1 = pendingRawByte;
  if (b1 < 0) {
    uint8_t raw1 = 0;
    if (!popRxByte(raw1)) return false;
    b1 = static_cast<int>(raw1);
  } else {
    pendingRawByte = -1;
  }

  if (b1 < 0x80) {
    // Short form: just a data byte, no prefix
    byte = static_cast<uint8_t>(b1);
    return true;
  }

  // Full enhanced protocol: need two bytes
  uint8_t raw2 = 0;
  if (!popRxByte(raw2)) {
    pendingRawByte = b1;
    return false;
  }
  int b2 = static_cast<int>(raw2);

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

#endif