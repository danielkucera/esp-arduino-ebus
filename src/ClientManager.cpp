#if defined(EBUS_INTERNAL)
#include "ClientManager.hpp"
#include "Logger.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cerrno>
#include <esp_timer.h>
#include <fcntl.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>

#include <algorithm>
#include <string>

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

  for (;;) {
    if (self->stopRunner) vTaskDelete(NULL);

    // Accept new clients and clean up disconnected ones
    self->acceptClients();

    vTaskDelay(100);
  }
}

void ClientManager::acceptClients() {
  // Accept read-only clients
  for (;;) {
    const int clientFd = acceptClient(readonlyServer);
    if (clientFd < 0) break;
    logger.debug("Accepted read-only client fd=" + std::to_string(clientFd));
    clients.push_back(make_unique<ReadOnlyClient>(clientFd, bus, request));
  }

  // Accept regular clients
  for (;;) {
    const int clientFd = acceptClient(regularServer);
    if (clientFd < 0) break;
    logger.debug("Accepted regular client fd=" + std::to_string(clientFd));
    clients.push_back(make_unique<RegularClient>(clientFd, bus, request));
  }

  // Accept enhanced clients
  for (;;) {
    const int clientFd = acceptClient(enhancedServer);
    if (clientFd < 0) break;
    logger.debug("Accepted enhanced client fd=" + std::to_string(clientFd));
    clients.push_back(make_unique<EnhancedClient>(clientFd, bus, request));
  }

  // Clean up disconnected clients
  clients.erase(
      std::remove_if(clients.begin(), clients.end(),
                     [](const std::unique_ptr<AbstractClient>& client) {
                       if (!client->isConnected()) {
                         logger.debug("Removing disconnected client");
                         client->stop();  // <-- ensure socket is closed
                         return true;
                       }
                       return false;
                     }),
      clients.end());
}

#endif
