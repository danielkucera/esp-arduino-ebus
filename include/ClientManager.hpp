#pragma once

#if defined(EBUS_INTERNAL)

#include <cstdint>

#include <vector>

#include "ClientType.hpp"

using LastCommsCallback = std::function<void()>;

// ClientManager handles all connected clients and routes data between them and
// the eBus It supports ReadOnly, Regular, and Enhanced clients.

class ClientManager {
 public:
  ClientManager();

  void setLastCommsCallback(LastCommsCallback callback);

  void start(ebus::Bus* bus, ebus::BusHandler* busHandler,
             ebus::Request* request);

  void stop();

 private:
    struct ServerSocket {
      uint16_t port;
      int listenFd = -1;
    };

    ServerSocket readonlyServer{3334};
    ServerSocket regularServer{3333};
    ServerSocket enhancedServer{3335};

  ebus::Queue<uint8_t>* clientByteQueue = nullptr;
  volatile bool stopRunner = false;
  volatile bool busRequested = false;

  ebus::Bus* bus = nullptr;
  ebus::BusHandler* busHandler = nullptr;
  ebus::Request* request = nullptr;

  LastCommsCallback lastCommsCallback = nullptr;

  std::vector<std::unique_ptr<AbstractClient>> clients;

  enum class BusState { Idle, Request, Transmit, Response };

  TaskHandle_t clientManagerTaskHandle;

  static void taskFunc(void* arg);

  static bool createListenSocket(ServerSocket& server);
  static int acceptClient(ServerSocket& server);
  void acceptClients();
};

extern ClientManager clientManager;
#endif
