#pragma once

#if defined(EBUS_INTERNAL)
#include <Ebus.h>

// Abstract base class for all client types
class AbstractClient {
 public:
  AbstractClient(int socketFd, ebus::Request* request, bool write);

  virtual bool available() const = 0;
  virtual bool readByte(uint8_t& byte) = 0;
  virtual bool writeBytes(const std::vector<uint8_t>& bytes) = 0;
  virtual bool handleBusData(const uint8_t& byte) = 0;

  bool isWriteCapable() const;
  bool isConnected() const;
  void stop();

 protected:
  int socketFd;
  ebus::Request* request;
  bool write;
};

// ReadOnly client: only sends, never receives
class ReadOnlyClient : public AbstractClient {
 public:
  ReadOnlyClient(int socketFd, ebus::Request* request);

  bool available() const override;
  bool readByte(uint8_t& byte) override;
  bool writeBytes(const std::vector<uint8_t>& bytes) override;
  bool handleBusData(const uint8_t& byte) override;
};

// Regular client: 1 byte per message
class RegularClient : public AbstractClient {
 public:
  RegularClient(int socketFd, ebus::Request* request);

  bool available() const override;
  bool readByte(uint8_t& byte) override;
  bool writeBytes(const std::vector<uint8_t>& bytes) override;
  bool handleBusData(const uint8_t& byte) override;
};

// Enhanced client: 1 or 2 bytes per message (protocol encoding/decoding)
class EnhancedClient : public AbstractClient {
 public:
  EnhancedClient(int socketFd, ebus::Request* request);

  bool available() const override;
  bool readByte(uint8_t& byte) override;
  bool writeBytes(const std::vector<uint8_t>& bytes) override;
  bool handleBusData(const uint8_t& byte) override;
};

#endif