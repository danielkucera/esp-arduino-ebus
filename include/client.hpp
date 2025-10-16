#pragma once

#include <WiFiClient.h>
#include <WiFiServer.h>

bool handleNewClient(WiFiServer* server, WiFiClient clients[]);

void handleClient(WiFiClient* client);
int pushClient(WiFiClient* client, uint8_t byte);

void handleClientEnhanced(WiFiClient* client);
int pushClientEnhanced(WiFiClient* client, uint8_t c, uint8_t d, bool log);

#if defined(EBUS_INTERNAL)
#include <Ebus.h>

#include <vector>

// C++11 compatible make_unique
template <class T, class... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

// Abstract base class for all client types
class AbstractClient {
 public:
  AbstractClient(WiFiClient* client, ebus::Request* request, bool write);

  virtual bool available() const = 0;
  virtual bool readByte(uint8_t& byte) = 0;
  virtual bool writeBytes(const std::vector<uint8_t>& bytes) = 0;
  virtual bool handleBusData(const uint8_t& byte) = 0;

  bool isWriteCapable() const;
  bool isConnected() const;
  void stop();

 protected:
  WiFiClient* client;
  ebus::Request* request;
  bool write;
};

// ReadOnly client: only sends, never receives
class ReadOnlyClient : public AbstractClient {
 public:
  ReadOnlyClient(WiFiClient* client, ebus::Request* request);

  bool available() const override;
  bool readByte(uint8_t& byte) override;
  bool writeBytes(const std::vector<uint8_t>& bytes) override;
  bool handleBusData(const uint8_t& byte) override;
};

// Regular client: 1 byte per message
class RegularClient : public AbstractClient {
 public:
  RegularClient(WiFiClient* client, ebus::Request* request);

  bool available() const override;
  bool readByte(uint8_t& byte) override;
  bool writeBytes(const std::vector<uint8_t>& bytes) override;
  bool handleBusData(const uint8_t& byte) override;
};

// Enhanced client: 1 or 2 bytes per message (protocol encoding/decoding)
class EnhancedClient : public AbstractClient {
 public:
  EnhancedClient(WiFiClient* client, ebus::Request* request);

  bool available() const override;
  bool readByte(uint8_t& byte) override;
  bool writeBytes(const std::vector<uint8_t>& bytes) override;
  bool handleBusData(const uint8_t& byte) override;
};

class ClientManager {
 public:
  ClientManager();

  void start(ebus::Bus* bus, ebus::Request* request,
             ebus::ServiceRunnerFreeRtos* serviceRunner);

  void stop();

 private:
  WiFiServer readonlyServer;
  WiFiServer regularServer;
  WiFiServer enhancedServer;

  ebus::Queue<uint8_t>* clientByteQueue = nullptr;
  volatile bool stopRunner = false;
  volatile bool busRequested = false;

  ebus::Request* request = nullptr;
  ebus::ServiceRunnerFreeRtos* serviceRunner = nullptr;

  std::vector<std::unique_ptr<AbstractClient>> clients;

  enum class BusState { Idle, Request, Transmit, Response };

  TaskHandle_t clientManagerTaskHandle;

  static void taskFunc(void* arg);

  void acceptClients();
};

extern ClientManager clientManager;
#endif
