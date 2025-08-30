#pragma once

#include <WiFiClient.h>
#include <WiFiServer.h>

bool handleNewClient(WiFiServer* server, WiFiClient clients[]);

void handleClient(WiFiClient* client);
bool getClientData(WiFiClient* client, uint8_t& byte);
int pushClient(WiFiClient* client, uint8_t byte);

void handleClientEnhanced(WiFiClient* client);
bool getClientDataEnhanced(WiFiClient* client, uint8_t& byte);
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
  explicit AbstractClient(WiFiClient* client, bool write);

  virtual bool available() const = 0;
  virtual bool readByte(uint8_t& byte) = 0;
  virtual bool writeBytes(const std::vector<uint8_t>& bytes) = 0;

  bool isWriteCapable() const;
  bool isConnected() const;
  void stop();

 protected:
  WiFiClient* client = nullptr;
  bool write = false;
};

// Regular client: 1 byte per message
class RegularClient : public AbstractClient {
 public:
  explicit RegularClient(WiFiClient* client);

  bool available() const override;
  bool readByte(uint8_t& byte) override;
  bool writeBytes(const std::vector<uint8_t>& bytes) override;
};

// ReadOnly client: only sends, never receives
class ReadOnlyClient : public AbstractClient {
 public:
  explicit ReadOnlyClient(WiFiClient* client);

  bool available() const override;
  bool readByte(uint8_t&) override;
  bool writeBytes(const std::vector<uint8_t>& bytes) override;
};

// Enhanced client: 1 or 2 bytes per message (protocol encoding/decoding)
class EnhancedClient : public AbstractClient {
 public:
  explicit EnhancedClient(WiFiClient* client);

  bool available() const override;
  bool readByte(uint8_t& byte) override;
  bool writeBytes(const std::vector<uint8_t>& bytes) override;
};

class ClientManager {
 public:
  ClientManager();

  void start(ebus::Queue<uint8_t>* clientByteQueue);

  static void stop();

 private:
  WiFiServer regularServer;
  WiFiServer readonlyServer;
  WiFiServer enhancedServer;

  std::vector<std::unique_ptr<AbstractClient>> clients;

  ebus::Queue<uint8_t>* clientByteQueue = nullptr;

  TaskHandle_t clientManagerTaskHandle;

  static void taskFunc(void* arg);

  void acceptClients();

  void processClients();

  void processBusData();
};

extern ClientManager clientManager;
#endif