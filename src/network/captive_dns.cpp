#include "network/captive_dns.hpp"

#include <lwip/ip_addr.h>

#include <cstdio>

#include "network/dns_server.hpp"
#include "system/logger.hpp"

namespace {

constexpr uint16_t captive_dns_port = 53;
constexpr const char* captive_dns_ip_string = "192.168.4.1";
const esp_ip4_addr_t kCaptiveDnsIp = {.addr = ESP_IP4TOADDR(192, 168, 4, 1)};

DNSServer captiveDnsServer;

}  // namespace

void startCaptiveDns() {
  if (captiveDnsServer.start(captive_dns_port, "*", kCaptiveDnsIp)) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Captive DNS started on %s",
             captive_dns_ip_string);
    logger.info(buf);
    return;
  }

  logger.warn("Captive DNS start failed");
}

TaskHandle_t getCaptiveDnsTaskHandle() {
  return captiveDnsServer.getTaskHandle();
}
