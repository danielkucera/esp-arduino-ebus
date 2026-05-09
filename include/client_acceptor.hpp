#pragma once

#if defined(EBUS_INTERNAL)
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdint>
#include <ebus/controller.hpp>
#include <ebus_accessor.hpp>
#include <memory>
#include <vector>

// ClientManager handles all connected clients and routes data between them and
// the eBus It supports ReadOnly, Regular, and Enhanced clients.

class ClientAcceptor {
 public:
  ClientAcceptor();

  void start();
  void stop();

  TaskHandle_t getTaskHandle() const { return client_acceptor_task_handle_; }
  size_t getClientsCount() const { return clients_.size(); }

 private:
  struct ServerSocket {
    uint16_t port;
    int listen_fd = -1;
  };

  ServerSocket readonly_server_{3334};
  ServerSocket regular_server_{3333};
  ServerSocket enhanced_server_{3335};

  ebus::Controller& controller_;

  struct ConnectedClient {
    int fd;
  };
  std::vector<ConnectedClient> clients_;

  portMUX_TYPE clients_mux_ = portMUX_INITIALIZER_UNLOCKED;

  TaskHandle_t client_acceptor_task_handle_;

  static void taskFunc(void* arg);

  static bool createListenSocket(ServerSocket& server);
  static int acceptClient(ServerSocket& server);
  void acceptClients();
  void closeListenSockets();
};

extern ClientAcceptor client_acceptor;
#endif
