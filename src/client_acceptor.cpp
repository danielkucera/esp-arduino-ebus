#if defined(EBUS_INTERNAL)
#include "client_acceptor.hpp"

#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>

#include <algorithm>
#include <cerrno>

#include "Logger.hpp"

ClientAcceptor client_acceptor;

ClientAcceptor::ClientAcceptor() : controller_(getEbusController()) {}

void ClientAcceptor::start() {
  createListenSocket(readonly_server_);
  createListenSocket(regular_server_);
  createListenSocket(enhanced_server_);

  // Start the clientManagerRunner task
  xTaskCreate(&ClientAcceptor::taskFunc, "clientAcceptorRunner", 2048, this, 3,
              &client_acceptor_task_handle_);
}

void ClientAcceptor::stop() {
  if (client_acceptor_task_handle_ != nullptr)
    xTaskNotifyGive(client_acceptor_task_handle_);

  // Ensure listening sockets are closed when stopping
  closeListenSockets();
}

bool ClientAcceptor::createListenSocket(ServerSocket& server) {
  if (server.listen_fd >= 0) return true;

  server.listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (server.listen_fd < 0) return false;

  int enable = 1;
  setsockopt(server.listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable,
             sizeof(enable));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(server.port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(server.listen_fd, reinterpret_cast<sockaddr*>(&addr),
           sizeof(addr)) != 0) {
    close(server.listen_fd);
    server.listen_fd = -1;
    return false;
  }

  if (listen(server.listen_fd, 4) != 0) {
    close(server.listen_fd);
    server.listen_fd = -1;
    return false;
  }

  int flags = fcntl(server.listen_fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(server.listen_fd, F_SETFL, flags | O_NONBLOCK);
  }

  return true;
}

int ClientAcceptor::acceptClient(ServerSocket& server) {
  if (server.listen_fd < 0 && !createListenSocket(server)) return -1;

  sockaddr_in addr{};
  socklen_t addr_len = sizeof(addr);
  const int client_fd =
      accept(server.listen_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len);
  if (client_fd < 0) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) return -1;
    return -1;
  }

  int flag = 1;

  if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) <
      0) {
    logger.warn("Failed to set TCP_NODELAY on client socket (fd=" +
                std::to_string(client_fd) + "): " + std::to_string(errno));
  }

  return client_fd;
}

void ClientAcceptor::taskFunc(void* arg) {
  ClientAcceptor* self = static_cast<ClientAcceptor*>(arg);

  for (;;) {
    if (ulTaskNotifyTake(pdTRUE, 0) > 0) {
      self->client_acceptor_task_handle_ = nullptr;
      vTaskDelete(NULL);
    }

    self->acceptClients();

    std::vector<int> client_fds;
    {
      portENTER_CRITICAL(&self->clients_mux_);
      client_fds.reserve(self->clients_.size());
      for (const auto& c : self->clients_) {
        client_fds.push_back(c.fd);
      }
      portEXIT_CRITICAL(&self->clients_mux_);
    }

    std::vector<int> closed_fds;
    for (int fd : client_fds) {
      char buffer = 0;
      int res = recv(fd, &buffer, 1, MSG_PEEK | MSG_DONTWAIT);
      if (res == 0 || (res < 0 && errno != EWOULDBLOCK && errno != EAGAIN)) {
        closed_fds.push_back(fd);
      }
    }

    if (!closed_fds.empty()) {
      for (int fd : closed_fds) {
        logger.info("TCP Client FD " + std::to_string(fd) + " closed.");
        self->controller_.removeClient(fd);
        close(fd);
      }

      portENTER_CRITICAL(&self->clients_mux_);
      self->clients_.erase(
          std::remove_if(self->clients_.begin(), self->clients_.end(),
                         [&](const ConnectedClient& c) {
                           return std::find(closed_fds.begin(),
                                            closed_fds.end(),
                                            c.fd) != closed_fds.end();
                         }),
          self->clients_.end());
      portEXIT_CRITICAL(&self->clients_mux_);
    }

    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
  }
}

void ClientAcceptor::acceptClients() {
  auto register_new = [this](ServerSocket& server, ebus::ClientType type,
                             const char* label) {
    for (;;) {
      const int client_fd = acceptClient(server);
      if (client_fd < 0) break;

      logger.info(std::string(label) +
                  " client connected (FD=" + std::to_string(client_fd) + ")");
      controller_.addClient(client_fd, type);
      portENTER_CRITICAL(&clients_mux_);
      clients_.push_back({client_fd});  // Protected access
      portEXIT_CRITICAL(&clients_mux_);
    }
  };

  register_new(readonly_server_, ebus::ClientType::read_only, "ReadOnly");
  register_new(regular_server_, ebus::ClientType::regular, "Regular");
  register_new(enhanced_server_, ebus::ClientType::enhanced, "Enhanced");
}

void ClientAcceptor::closeListenSockets() {
  if (readonly_server_.listen_fd >= 0) close(readonly_server_.listen_fd);
  if (regular_server_.listen_fd >= 0) close(regular_server_.listen_fd);
  if (enhanced_server_.listen_fd >= 0) close(enhanced_server_.listen_fd);
  readonly_server_.listen_fd = -1;
  regular_server_.listen_fd = -1;
  enhanced_server_.listen_fd = -1;
}

#endif
