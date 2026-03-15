#include "DNSServer.h"
#include "Logger.hpp"

#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <lwip/ip4_addr.h>
#include <lwip/sockets.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr size_t kDnsHeaderSize = 12;
constexpr size_t kMaxPacketSize = 512;
}  // namespace

DNSServer::DNSServer() = default;

DNSServer::~DNSServer() { stop(); }

bool DNSServer::start(uint16_t port, const char* domainName,
                      const esp_ip4_addr_t& resolvedIp) {
  stop();
  port_ = port;
  domain_ = domainName != nullptr ? domainName : "*";
  resolvedIp_ = resolvedIp;

  socketFd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socketFd_ < 0) return false;

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_);
  if (bind(socketFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    stop();
    return false;
  }

  int flags = fcntl(socketFd_, F_GETFL, 0);
  fcntl(socketFd_, F_SETFL, flags | O_NONBLOCK);

  if (taskHandle_ == nullptr) {
    xTaskCreate(taskEntry, "dns_task", 4096, this, 1, &taskHandle_);
  }

  return true;
}

void DNSServer::taskEntry(void* arg) {
  auto* server = static_cast<DNSServer*>(arg);
  if (server == nullptr) {
    vTaskDelete(nullptr);
    return;
  }
  server->taskLoop();
}

void DNSServer::taskLoop() {
  while (true) {
    processNextRequest();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void DNSServer::processNextRequest() {
  if (socketFd_ < 0) return;

  uint8_t buffer[kMaxPacketSize];
  sockaddr_in client{};
  socklen_t clientLen = sizeof(client);
  int received = recvfrom(socketFd_, buffer, sizeof(buffer), 0,
                          reinterpret_cast<sockaddr*>(&client), &clientLen);
  if (received <= 0) return;
  logger.debug("Received DNS request from " + std::string(inet_ntoa(client.sin_addr)));
  if (static_cast<size_t>(received) < kDnsHeaderSize) return;

  if (buffer[2] & 0x80) return;  // response packet
  uint16_t qdcount = (buffer[4] << 8) | buffer[5];
  if (qdcount == 0) return;

  size_t idx = kDnsHeaderSize;
  while (idx < static_cast<size_t>(received) && buffer[idx] != 0) {
    idx += buffer[idx] + 1;
  }
  if (idx + 5 > static_cast<size_t>(received)) return;

  size_t questionLen = (idx + 5) - kDnsHeaderSize;
  size_t responseLen = kDnsHeaderSize + questionLen;

  uint8_t response[kMaxPacketSize];
  std::memcpy(response, buffer, responseLen);

  response[2] = 0x81;
  response[3] = 0x80;
  response[6] = 0x00;
  response[7] = 0x01;  // answer count = 1
  response[8] = 0x00;
  response[9] = 0x00;
  response[10] = 0x00;
  response[11] = 0x00;

  response[responseLen++] = 0xC0;
  response[responseLen++] = 0x0C;
  response[responseLen++] = 0x00;
  response[responseLen++] = 0x01;
  response[responseLen++] = 0x00;
  response[responseLen++] = 0x01;
  response[responseLen++] = 0x00;
  response[responseLen++] = 0x00;
  response[responseLen++] = 0x00;
  response[responseLen++] = 0x00;
  response[responseLen++] = 0x00;
  response[responseLen++] = 0x04;

  const ip4_addr_t* ip = reinterpret_cast<const ip4_addr_t*>(&resolvedIp_);
  response[responseLen++] = ip4_addr1_16(ip);
  response[responseLen++] = ip4_addr2_16(ip);
  response[responseLen++] = ip4_addr3_16(ip);
  response[responseLen++] = ip4_addr4_16(ip);

  sendto(socketFd_, response, responseLen, 0,
         reinterpret_cast<sockaddr*>(&client), clientLen);
}

void DNSServer::stop() {
  if (socketFd_ >= 0) {
    close(socketFd_);
    socketFd_ = -1;
  }
}
