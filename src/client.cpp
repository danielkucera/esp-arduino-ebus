#include "client.hpp"

#include <cerrno>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <lwip/sockets.h>
#include <lwip/tcp.h>

#include "BusType.hpp"
#include "main.hpp"

#define M1 0b11000000
#define M2 0b10000000

enum requests { CMD_INIT = 0, CMD_SEND, CMD_START, CMD_INFO };

namespace {

TaskHandle_t clientAcceptTaskHandle = nullptr;
int acceptServerFd = -1;
int* acceptClients = nullptr;
int acceptEnhancedServerFd = -1;
int* acceptEnhancedClients = nullptr;
int acceptReadOnlyServerFd = -1;
int* acceptReadOnlyClients = nullptr;

void clientAcceptTask(void* arg) {
  for (;;) {
    handleNewClient(acceptServerFd, acceptClients);
    handleNewClient(acceptEnhancedServerFd, acceptEnhancedClients);
    handleNewClient(acceptReadOnlyServerFd, acceptReadOnlyClients);
    vTaskDelay(1);
  }
}

bool isSocketConnected(int clientFd) {
  if (clientFd < 0) return false;
  char buffer = 0;
  const int result = recv(clientFd, &buffer, 1, MSG_PEEK | MSG_DONTWAIT);
  if (result > 0) return true;
  if (result == 0) return false;
  return errno == EWOULDBLOCK || errno == EAGAIN;
}

void closeSocket(int& clientFd) {
  if (clientFd >= 0) {
    shutdown(clientFd, SHUT_RDWR);
    close(clientFd);
    clientFd = -1;
  }
}

int socketAvailable(int clientFd) {
  if (clientFd < 0) return 0;
  int pending = 0;
  if (lwip_ioctl(clientFd, FIONREAD, &pending) == 0) {
    return pending;
  }
  return 0;
}

int socketReadByte(int clientFd, int flags = MSG_DONTWAIT) {
  if (clientFd < 0) return -1;
  uint8_t byte = 0;
  const int result = recv(clientFd, &byte, 1, flags);
  if (result <= 0) return -1;
  return byte;
}

int socketPeekByte(int clientFd) {
  return socketReadByte(clientFd, MSG_PEEK | MSG_DONTWAIT);
}

size_t socketWriteBytes(int clientFd, const uint8_t* data, size_t size) {
  if (clientFd < 0 || data == nullptr || size == 0) return 0;
  const int result = send(clientFd, data, size, 0);
  if (result < 0) return 0;
  return static_cast<size_t>(result);
}

size_t socketWriteString(int clientFd, const char* message) {
  if (message == nullptr) return 0;
  return socketWriteBytes(clientFd, reinterpret_cast<const uint8_t*>(message),
                          strlen(message));
}

int socketAvailableForWrite(int clientFd) {
  return isSocketConnected(clientFd) ? 1 : 0;
}

}  // namespace

bool startClientAcceptTask(int serverFd, int clients[], int enhancedServerFd,
                           int enhancedClients[], int readOnlyServerFd,
                           int readOnlyClients[]) {
  if (clientAcceptTaskHandle != nullptr) return true;

  acceptServerFd = serverFd;
  acceptClients = clients;
  acceptEnhancedServerFd = enhancedServerFd;
  acceptEnhancedClients = enhancedClients;
  acceptReadOnlyServerFd = readOnlyServerFd;
  acceptReadOnlyClients = readOnlyClients;

  return xTaskCreate(clientAcceptTask, "client_accept", 4096, nullptr, 1,
                     &clientAcceptTaskHandle) == pdPASS;
}

void stopClientAcceptTask() {
  if (clientAcceptTaskHandle != nullptr) {
    vTaskDelete(clientAcceptTaskHandle);
    clientAcceptTaskHandle = nullptr;
  }
}

bool handleNewClient(int serverFd, int clients[]) {
  sockaddr_in addr{};
  socklen_t addrLen = sizeof(addr);
  const int clientFd =
      accept(serverFd, reinterpret_cast<sockaddr*>(&addr), &addrLen);
  if (clientFd < 0) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) return false;
    return false;
  }

  // Find free/disconnected slot
  int i;
  for (i = 0; i < MAX_WIFI_CLIENTS; i++) {
    if (!isSocketConnected(clients[i])) {
      closeSocket(clients[i]);
      clients[i] = clientFd;
      int noDelay = 1;
      setsockopt(clients[i], IPPROTO_TCP, TCP_NODELAY, &noDelay,
                 sizeof(noDelay));
      break;
    }
  }

  // No free/disconnected slot so reject
  if (i == MAX_WIFI_CLIENTS) {
    static const char busyMessage[] = "busy\r\n";
    socketWriteBytes(clientFd, reinterpret_cast<const uint8_t*>(busyMessage),
                     sizeof(busyMessage) - 1);
    int rejectFd = clientFd;
    closeSocket(rejectFd);
  }

  return true;
}

void handleClient(int* clientFd) {
  while (socketAvailable(*clientFd) && Bus.availableForWrite() > 0) {
    // working char by char is not very efficient
    const int value = socketReadByte(*clientFd);
    if (value < 0) break;
    Bus.write(static_cast<uint8_t>(value));
  }
}

int pushClient(int* clientFd, uint8_t byte) {
  if (socketAvailableForWrite(*clientFd) >= AVAILABLE_THRESHOLD) {
    socketWriteBytes(*clientFd, &byte, 1);
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

void send_res(int* clientFd, uint8_t c, uint8_t d) {
  uint8_t data[2];
  encode(c, d, data);
  socketWriteBytes(*clientFd, data, 2);
}

void process_cmd(int* clientFd, uint8_t c, uint8_t d) {
  if (c == CMD_INIT) {
    send_res(clientFd, RESETTED, 0x0);
    return;
  }
  if (c == CMD_START) {
    if (d == SYN) {
      clearArbitrationClient();
      DEBUG_LOG("CMD_START SYN\n");
      return;
    } else {
      // start arbitration
      int cl = *clientFd;
      uint8_t ad = d;
      int arbitrationClientFd = *clientFd;
      if (!setArbitrationClient(arbitrationClientFd, d)) {
        if (cl != arbitrationClientFd) {
          // only one client can be in arbitration
          DEBUG_LOG("CMD_START ONGOING 0x%02 0x%02x\n", ad, d);
          send_res(clientFd, ERROR_HOST, ERR_FRAMING);
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
  if (c == CMD_SEND) {
    DEBUG_LOG("SEND 0x%02x\n", d);
    Bus.write(d);
    return;
  }
  if (c == CMD_INFO) {
    // if needed, set bit 0 as reply to INIT command
    return;
  }
}

bool read_cmd(int* clientFd, uint8_t (&data)[2]) {
  int b, b2;

  b = socketReadByte(*clientFd);

  if (b < 0) {
    // available and read -1 ???
    return false;
  }

  if (b < 0b10000000) {
    data[0] = CMD_SEND;
    data[1] = b;
    return true;
  }

  if (b < 0b11000000) {
    DEBUG_LOG("first command signature error\n");
    socketWriteString(*clientFd, "first command signature error");
    // first command signature error
    closeSocket(*clientFd);
    return false;
  }

  b2 = socketReadByte(*clientFd);

  if (b2 < 0) {
    // second command missing
    DEBUG_LOG("second command missing\n");
    socketWriteString(*clientFd, "second command missing");
    closeSocket(*clientFd);
    return false;
  }

  if ((b2 & 0b11000000) != 0b10000000) {
    // second command signature error
    DEBUG_LOG("second command signature error\n");
    socketWriteString(*clientFd, "second command signature error");
    closeSocket(*clientFd);
    return false;
  }

  decode(b, b2, data);
  return true;
}

void handleClientEnhanced(int* clientFd) {
  while (socketAvailable(*clientFd)) {
    uint8_t data[2];
    if (read_cmd(clientFd, data)) {
      process_cmd(clientFd, data[0], data[1]);
    }
  }
}

int pushClientEnhanced(int* clientFd, uint8_t c, uint8_t d, bool log) {
  if (log) {
    DEBUG_LOG("DATA           0x%02x 0x%02x\n", c, d);
  }
  if (socketAvailableForWrite(*clientFd) >= AVAILABLE_THRESHOLD) {
    send_res(clientFd, c, d);
    return 1;
  }
  return 0;
}
