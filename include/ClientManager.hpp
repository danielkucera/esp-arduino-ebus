#pragma once

#if defined(EBUS_INTERNAL)
#include <WiFiServer.h>

#include <vector>

#include "ClientType.hpp"

using LastCommsCallback = std::function<void()>;

// ClientManager handles all connected clients and routes data between them and the eBus
// It supports ReadOnly, Regular, and Enhanced clients.

class ClientManager {
 public:
  ClientManager();

  void setLastCommsCallback(LastCommsCallback callback);

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

  LastCommsCallback lastCommsCallback = nullptr;

  std::vector<std::unique_ptr<AbstractClient>> clients;

  enum class BusState { Idle, Request, Transmit, Response };

  TaskHandle_t clientManagerTaskHandle;

  static void taskFunc(void* arg);

  void acceptClients();
};

extern ClientManager clientManager;
#endif
