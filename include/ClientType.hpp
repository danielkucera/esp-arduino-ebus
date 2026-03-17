#pragma once

#if defined(EBUS_INTERNAL)
#include <Ebus.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <vector>

// Abstract base class for all client types
class AbstractClient {
 public:
  AbstractClient(int socketFd, ebus::Bus* bus, ebus::Request* request,
                 bool write);
  virtual ~AbstractClient();

  virtual bool available() const = 0;
  virtual bool readByte(uint8_t& byte) = 0;
  virtual bool writeBytes(const std::vector<uint8_t>& bytes) = 0;

  bool isWriteCapable() const;
  bool isConnected() const;
  void stop();

 protected:
  static constexpr UBaseType_t RX_QUEUE_LENGTH = 256;

  bool popRxByte(uint8_t& byte);
  bool peekRxByte(uint8_t& byte);
  size_t availableRx() const;

  int socketFd;
  ebus::Bus* bus;
  ebus::Request* request;
  bool write;
  volatile bool connected = false;
  volatile bool stopReader = false;
  QueueHandle_t rxQueue = nullptr;
  TaskHandle_t readerTask = nullptr;

 private:
  static void readerTaskEntry(void* arg);
  void readerTaskLoop();
};

// ReadOnly client: only sends, never receives
class ReadOnlyClient : public AbstractClient {
 public:
  ReadOnlyClient(int socketFd, ebus::Bus* bus, ebus::Request* request);

  bool available() const override;
  bool readByte(uint8_t& byte) override;
  bool writeBytes(const std::vector<uint8_t>& bytes) override;
};

// Regular client: 1 byte per message
class RegularClient : public AbstractClient {
 public:
  RegularClient(int socketFd, ebus::Bus* bus, ebus::Request* request);

  bool available() const override;
  bool readByte(uint8_t& byte) override;
  bool writeBytes(const std::vector<uint8_t>& bytes) override;
};

// Enhanced client: 1 or 2 bytes per message (protocol encoding/decoding)
class EnhancedClient : public AbstractClient {
 public:
  EnhancedClient(int socketFd, ebus::Bus* bus, ebus::Request* request);

  bool available() const override;
  bool readByte(uint8_t& byte) override;
  bool writeBytes(const std::vector<uint8_t>& bytes) override;

 private:
  int pendingRawByte = -1;
};

#endif