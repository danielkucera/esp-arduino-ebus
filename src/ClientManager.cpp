#if defined(EBUS_INTERNAL)
#include "ClientManager.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cerrno>
#include <fcntl.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>

#include <algorithm>

#include "Logger.hpp"

// C++11 compatible make_unique
template <class T, class... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

ClientManager clientManager;

ClientManager::ClientManager() = default;

void ClientManager::start(ebus::Bus* bus, ebus::BusHandler* busHandler,
                          ebus::Request* request) {
  createListenSocket(readonlyServer);
  createListenSocket(regularServer);
  createListenSocket(enhancedServer);

  this->bus = bus;
  this->busHandler = busHandler;
  this->request = request;

  clientByteQueue = new ebus::Queue<uint8_t>();

  request->setExternalBusRequestedCallback([this]() { busRequested = true; });

  busHandler->addByteListener(
      [this](const uint8_t& byte) { clientByteQueue->try_push(byte); });

  // Start the clientManagerRunner task
  xTaskCreate(&ClientManager::taskFunc, "clientManagerRunner", 4096, this, 3,
              &clientManagerTaskHandle);
}

void ClientManager::stop() { stopRunner = true; }

bool ClientManager::createListenSocket(ServerSocket& server) {
  if (server.listenFd >= 0) return true;

  server.listenFd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (server.listenFd < 0) return false;

  int enable = 1;
  setsockopt(server.listenFd, SOL_SOCKET, SO_REUSEADDR, &enable,
             sizeof(enable));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(server.port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(server.listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) !=
      0) {
    close(server.listenFd);
    server.listenFd = -1;
    return false;
  }

  if (listen(server.listenFd, 4) != 0) {
    close(server.listenFd);
    server.listenFd = -1;
    return false;
  }

  int flags = fcntl(server.listenFd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(server.listenFd, F_SETFL, flags | O_NONBLOCK);
  }

  return true;
}

int ClientManager::acceptClient(ServerSocket& server) {
  if (server.listenFd < 0 && !createListenSocket(server)) return -1;

  sockaddr_in addr{};
  socklen_t addrLen = sizeof(addr);
  const int clientFd =
      accept(server.listenFd, reinterpret_cast<sockaddr*>(&addr), &addrLen);
  if (clientFd < 0) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) return -1;
    return -1;
  }

  int flag = 1;
  setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

  return clientFd;
}

void ClientManager::taskFunc(void* arg) {
  ClientManager* self = static_cast<ClientManager*>(arg);
  AbstractClient* activeClient = nullptr;
  BusState busState = BusState::Idle;
  uint8_t receiveByte = 0;

  for (;;) {
    if (self->stopRunner) vTaskDelete(NULL);

    // Check for new clients
    self->acceptClients();

    // Clean up disconnected active client
    if (activeClient && !activeClient->isConnected()) {
      logger.info("Client disconnected (was active)");
      activeClient->stop();
      activeClient = nullptr;
      busState = BusState::Idle;
      self->busRequested = false;
      self->request->reset();
    }

    // Select new active client if idle
    if (!activeClient && busState == BusState::Idle) {
      for (size_t i = 0; i < self->clients.size(); ++i) {
        AbstractClient* client = self->clients[i].get();
        if (client->isConnected() && client->isWriteCapable() &&
            client->available()) {
          activeClient = client;
          busState = BusState::Request;
          self->busRequested = false;
          logger.info("Client has data to send (client #" + std::to_string(i) + ")");
          break;
        }
      }
    }

    // Request bus access
    if (activeClient && busState == BusState::Request) {
      if (self->request->busAvailable()) {
        uint8_t firstByte = 0;
        if (activeClient->readByte(firstByte)) {
          self->request->requestBus(firstByte, true);
          busState = BusState::Response;
        } else {
          // Client initialized or error
          activeClient = nullptr;
          busState = BusState::Idle;
          self->busRequested = false;
          self->request->reset();
        }
      }
    }

    // Transmit to bus if needed
    if (activeClient && busState == BusState::Transmit) {
      uint8_t sendByte = 0;
      if (activeClient->readByte(sendByte)) {
        self->bus->writeByte(sendByte);
        busState = BusState::Response;
      }
    }

    // Process received bytes from bus
    while (self->clientByteQueue->try_pop(receiveByte)) {
      if (activeClient) {
        if ((busState == BusState::Response ||
             busState == BusState::Transmit) &&
            self->busRequested) {
          if (activeClient->handleBusData(receiveByte)) {
            // Continue transmitting if needed
            busState = BusState::Transmit;
          } else {
            // Transaction done or error
            activeClient = nullptr;
            busState = BusState::Idle;
            self->busRequested = false;
            self->request->reset();
          }
        }
      }

      // Forward to all other clients
      for (size_t i = 0; i < self->clients.size(); ++i) {
        AbstractClient* client = self->clients[i].get();
        if (client != activeClient && client->isConnected()) {
          client->writeBytes({receiveByte});
        }
      }
    }

    vTaskDelay(1);
  }
}

void ClientManager::acceptClients() {
  // Accept read-only clients
  for (;;) {
    const int clientFd = acceptClient(readonlyServer);
    if (clientFd < 0) break;
    clients.push_back(make_unique<ReadOnlyClient>(clientFd, request));
    logger.info("ReadOnly client connected");
  }

  // Accept regular clients
  for (;;) {
    const int clientFd = acceptClient(regularServer);
    if (clientFd < 0) break;
    clients.push_back(make_unique<RegularClient>(clientFd, request));
    logger.info("Regular client connected");
  }

  // Accept enhanced clients
  for (;;) {
    const int clientFd = acceptClient(enhancedServer);
    if (clientFd < 0) break;
    clients.push_back(make_unique<EnhancedClient>(clientFd, request));
    logger.info("Enhanced client connected");
  }

  // Clean up disconnected clients
  clients.erase(
      std::remove_if(clients.begin(), clients.end(),
                     [](const std::unique_ptr<AbstractClient>& client) {
                       if (!client->isConnected()) {
                         logger.info("Client disconnected");
                         client->stop();  // <-- ensure socket is closed
                         return true;
                       }
                       return false;
                     }),
      clients.end());
}

#endif
