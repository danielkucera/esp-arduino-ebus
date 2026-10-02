#if !defined(EBUS_INTERNAL)

#include "bridge/client.hpp"

#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>

#include <cerrno>
#include <cstring>

#include "app/app_limits.hpp"
#include "bridge/bus_type.hpp"
#include "main.hpp"

#define M1 0b11000000
#define M2 0b10000000

enum Requests { cmd_init = 0, cmd_send, cmd_start, cmd_info };

namespace {

TaskHandle_t client_accept_task_handle = nullptr;
TaskHandle_t data_task_handle = nullptr;
int wifi_server_fd = -1;
int wifi_clients[MAX_WIFI_CLIENTS] = {-1, -1, -1, -1};

int wifi_server_enhanced_fd = -1;
int wifi_clients_enhanced[MAX_WIFI_CLIENTS] = {-1, -1, -1, -1};

int wifi_server_read_only_fd = -1;
int wifi_clients_read_only[MAX_WIFI_CLIENTS] = {-1, -1, -1, -1};

constexpr uint16_t port_default = 3333;
constexpr uint16_t port_enhanced = 3335;
constexpr uint16_t port_read_only = 3334;

bool createListenSocket(int& listen_fd, uint16_t port) {
  if (listen_fd >= 0) return true;

  listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (listen_fd < 0) return false;

  int enable = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(listen_fd);
    listen_fd = -1;
    return false;
  }

  if (listen(listen_fd, 4) != 0) {
    close(listen_fd);
    listen_fd = -1;
    return false;
  }

  int flags = fcntl(listen_fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);
  }

  return true;
}

bool createListenSockets() {
  return createListenSocket(wifi_server_fd, port_default) &&
         createListenSocket(wifi_server_enhanced_fd, port_enhanced) &&
         createListenSocket(wifi_server_read_only_fd, port_read_only);
}

void clientAcceptTask(void* arg) {
  for (;;) {
    handleNewClient(wifi_server_fd, wifi_clients);
    handleNewClient(wifi_server_enhanced_fd, wifi_clients_enhanced);
    handleNewClient(wifi_server_read_only_fd, wifi_clients_read_only);
    vTaskDelay(1);
  }
}

void dataProcess() {
  for (int i = 0; i < MAX_WIFI_CLIENTS; i++) {
    handleClient(&wifi_clients[i]);
    handleClientEnhanced(&wifi_clients_enhanced[i]);
  }

  BusType::Data data;
  if (bus.read(data)) {
    for (int i = 0; i < MAX_WIFI_CLIENTS; i++) {
      if (data.enhanced) {
        if (data.client_fd == wifi_clients_enhanced[i]) {
          pushClientEnhanced(&wifi_clients_enhanced[i], data.c, data.d, true);
        }
      } else {
        pushClient(&wifi_clients[i], data.d);
        pushClient(&wifi_clients_read_only[i], data.d);
        if (data.client_fd != wifi_clients_enhanced[i]) {
          pushClientEnhanced(&wifi_clients_enhanced[i], data.c, data.d,
                             data.log_to_client_fd == wifi_clients_enhanced[i]);
        }
      }
    }
  }
}

void dataLoop(void* arg) {
  (void)arg;
  for (;;) {
    dataProcess();
  }
}

bool isSocketConnected(int client_fd) {
  if (client_fd < 0) return false;
  char buffer = 0;
  const int result = recv(client_fd, &buffer, 1, MSG_PEEK | MSG_DONTWAIT);
  if (result > 0) return true;
  if (result == 0) return false;
  return errno == EWOULDBLOCK || errno == EAGAIN;
}

void closeSocket(int& client_fd) {
  if (client_fd >= 0) {
    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
    client_fd = -1;
  }
}

int socketAvailable(int client_fd) {
  if (client_fd < 0) return 0;
  int pending = 0;
  if (lwip_ioctl(client_fd, FIONREAD, &pending) == 0) {
    return pending;
  }
  return 0;
}

int socketReadByte(int client_fd, int flags = MSG_DONTWAIT) {
  if (client_fd < 0) return -1;
  uint8_t byte = 0;
  const int result = recv(client_fd, &byte, 1, flags);
  if (result <= 0) return -1;
  return byte;
}

size_t socketWriteBytes(int client_fd, const uint8_t* data, size_t size) {
  if (client_fd < 0 || data == nullptr || size == 0) return 0;
  const int result = send(client_fd, data, size, 0);
  if (result < 0) return 0;
  return static_cast<size_t>(result);
}

size_t socketWriteString(int client_fd, const char* message) {
  if (message == nullptr) return 0;
  return socketWriteBytes(client_fd, reinterpret_cast<const uint8_t*>(message),
                          strlen(message));
}

}  // namespace

bool startClientRuntime() {
  if (!createListenSockets()) return false;

  if (data_task_handle == nullptr) {
    if (xTaskCreate(dataLoop, "data_loop", app::limits::Task::data_loop_stack,
                    nullptr, app::limits::Task::data_loop_priority,
                    &data_task_handle) != pdPASS) {
      return false;
    }
  }

  if (client_accept_task_handle == nullptr) {
    if (xTaskCreate(clientAcceptTask, "client_accept",
                    app::limits::Task::client_accept_stack, nullptr,
                    app::limits::Task::client_accept_priority,
                    &client_accept_task_handle) != pdPASS) {
      if (data_task_handle != nullptr) {
        vTaskDelete(data_task_handle);
        data_task_handle = nullptr;
      }
      return false;
    }
  }

  return true;
}

void stopClientRuntime() {
  if (client_accept_task_handle != nullptr) {
    vTaskDelete(client_accept_task_handle);
    client_accept_task_handle = nullptr;
  }

  if (data_task_handle != nullptr) {
    vTaskDelete(data_task_handle);
    data_task_handle = nullptr;
  }
}

bool handleNewClient(int server_fd, int clients[]) {
  sockaddr_in addr{};
  socklen_t addr_len = sizeof(addr);
  const int client_fd =
      accept(server_fd, reinterpret_cast<sockaddr*>(&addr), &addr_len);
  if (client_fd < 0) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) return false;
    return false;
  }

  // Find free/disconnected slot
  int i;
  for (i = 0; i < MAX_WIFI_CLIENTS; i++) {
    if (!isSocketConnected(clients[i])) {
      closeSocket(clients[i]);
      clients[i] = client_fd;
      int no_delay = 1;
      setsockopt(clients[i], IPPROTO_TCP, TCP_NODELAY, &no_delay,
                 sizeof(no_delay));
      int flags = fcntl(clients[i], F_GETFL, 0);
      if (flags >= 0) {
        fcntl(clients[i], F_SETFL, flags | O_NONBLOCK);
      }
      break;
    }
  }

  // No free/disconnected slot so reject
  if (i == MAX_WIFI_CLIENTS) {
    static const char busy_message[] = "busy\r\n";
    socketWriteBytes(client_fd, reinterpret_cast<const uint8_t*>(busy_message),
                     sizeof(busy_message) - 1);
    int reject_fd = client_fd;
    closeSocket(reject_fd);
  }

  return true;
}

void handleClient(const int* client_fd) {
  while (socketAvailable(*client_fd) && bus.availableForWrite() > 0) {
    // working char by char is not very efficient
    const int value = socketReadByte(*client_fd);
    if (value < 0) break;
    bus.write(static_cast<uint8_t>(value));
  }
}

int pushClient(const int* client_fd, uint8_t byte) {
  if (isSocketConnected(*client_fd)) {
    socketWriteBytes(*client_fd, &byte, 1);
    return 1;
  }
  return 0;
}

void decode(int b1, int b2, uint8_t (&data)[2]) {
  data[0] = (b1 >> 2) & 0b1111;
  data[1] = ((b1 & 0b11) << 6) | (b2 & 0b00111111);
}

void encode(uint8_t c, uint8_t d, uint8_t (&data)[2]) {
  data[0] = M1 | c << 2 | d >> 6;
  data[1] = M2 | (d & 0b00111111);
}

void sendRes(const int* client_fd, uint8_t c, uint8_t d) {
  uint8_t data[2];
  encode(c, d, data);
  socketWriteBytes(*client_fd, data, 2);
}

void processCmd(const int* client_fd, uint8_t c, uint8_t d) {
  if (c == cmd_init) {
    sendRes(client_fd, resetted, 0x0);
    return;
  }
  if (c == cmd_start) {
    if (d == syn_byte) {
      clearArbitrationClient();
      DEBUG_LOG("CMD_START SYN\n");
      return;
    } else {
      // start arbitration
      uint8_t ad = d;
      int arbitration_client_fd = *client_fd;
      if (!setArbitrationClient(arbitration_client_fd, d)) {
        int cl = *client_fd;
        if (cl != arbitration_client_fd) {
          // only one client can be in arbitration
          DEBUG_LOG("CMD_START ONGOING 0x%02 0x%02x\n", ad, d);
          sendRes(client_fd, error_host, err_framing);
          return;
        } else {
          DEBUG_LOG("CMD_START REPEAT 0x%02x\n", d);
        }
      } else {
        DEBUG_LOG("CMD_START 0x%02x\n", d);
      }
      return;
    }
  }
  if (c == cmd_send) {
    DEBUG_LOG("SEND 0x%02x\n", d);
    bus.write(d);
    return;
  }
  if (c == cmd_info) {
    // if needed, set bit 0 as reply to INIT command
    return;
  }
}

bool readCmd(int* client_fd, uint8_t (&data)[2]) {
  int b, b2;

  b = socketReadByte(*client_fd);

  if (b < 0) {
    // available and read -1 ???
    return false;
  }

  if (b < 0b10000000) {
    data[0] = cmd_send;
    data[1] = b;
    return true;
  }

  if (b < 0b11000000) {
    DEBUG_LOG("first command signature error\n");
    socketWriteString(*client_fd, "first command signature error");
    // first command signature error
    closeSocket(*client_fd);
    return false;
  }

  b2 = socketReadByte(*client_fd);

  if (b2 < 0) {
    // second command missing
    DEBUG_LOG("second command missing\n");
    socketWriteString(*client_fd, "second command missing");
    closeSocket(*client_fd);
    return false;
  }

  if ((b2 & 0b11000000) != 0b10000000) {
    // second command signature error
    DEBUG_LOG("second command signature error\n");
    socketWriteString(*client_fd, "second command signature error");
    closeSocket(*client_fd);
    return false;
  }

  decode(b, b2, data);
  return true;
}

void handleClientEnhanced(int* client_fd) {
  while (socketAvailable(*client_fd)) {
    uint8_t data[2];
    if (readCmd(client_fd, data)) {
      processCmd(client_fd, data[0], data[1]);
    }
  }
}

int pushClientEnhanced(const int* client_fd, uint8_t c, uint8_t d, bool log) {
  if (log) {
    DEBUG_LOG("DATA           0x%02x 0x%02x\n", c, d);
  }
  if (isSocketConnected(*client_fd)) {
    sendRes(client_fd, c, d);
    return 1;
  }
  return 0;
}

#endif
