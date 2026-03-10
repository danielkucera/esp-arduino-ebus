#include "WiFi.h"

#include <cstdio>
#include <cstring>
#include <utility>

#include <esp_log.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <esp_idf_version.h>
#include <nvs_flash.h>
#include <arpa/inet.h>
#include <lwip/ip4_addr.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

namespace {
constexpr const char* kTag = "WiFiCompat";

IPAddress fromIpInfo(const esp_netif_ip_info_t& info) {
  IPAddress ip;
  ip.fromU32(ntohl(info.ip.addr));
  return ip;
}

IPAddress fromIp4(const esp_ip4_addr_t& addr) {
  IPAddress ip;
  ip.fromU32(ntohl(addr.addr));
  return ip;
}

esp_ip4_addr_t toIp4(const IPAddress& ip) {
  esp_ip4_addr_t addr{};
  uint32_t raw = ip.toU32();
  addr.addr = htonl(raw);
  return addr;
}

}  // namespace

WiFiClass WiFi;

WiFiClass::WiFiClass() = default;

void WiFiClass::ensureInit() {
  if (initialized_) return;

  static bool nvsReady = false;
  if (!nvsReady) {
    esp_err_t nvsErr = nvs_flash_init();
    if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      nvsErr = nvs_flash_init();
    }
    if (nvsErr != ESP_OK) {
      ESP_LOGE(kTag, "nvs_flash_init failed");
      return;
    }
    nvsReady = true;
  }

  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "esp_netif_init failed");
    return;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(kTag, "esp_event_loop_create_default failed");
    return;
  }

  if (staNetif_ == nullptr) {
    staNetif_ = esp_netif_create_default_wifi_sta();
  }
  if (apNetif_ == nullptr) {
    apNetif_ = esp_netif_create_default_wifi_ap();
  }

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&cfg) != ESP_OK) {
    ESP_LOGE(kTag, "esp_wifi_init failed");
    return;
  }

  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                      &WiFiClass::handleWifiEvent, this,
                                      nullptr);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                      &WiFiClass::handleIpEvent, this, nullptr);

  initialized_ = true;
}

void WiFiClass::onEvent(EventCallback cb) {
  ensureInit();
  callback_ = std::move(cb);
}

void WiFiClass::persistent(bool enable) {
  ensureInit();
  esp_wifi_set_storage(enable ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM);
}

void WiFiClass::setAutoReconnect(bool enable) {
  ensureInit();
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR < 5
  esp_wifi_set_auto_connect(enable);
#else
  (void)enable;
#endif
}

bool WiFiClass::setHostname(const char* hostname) {
  ensureInit();
  if (hostname == nullptr) return false;
  hostname_ = hostname;
  if (staNetif_ == nullptr) return false;
  return esp_netif_set_hostname(staNetif_, hostname) == ESP_OK;
}

bool WiFiClass::mode(wifi_mode_t mode) {
  ensureInit();
  if (!initialized_) return false;
  if (esp_wifi_set_mode(mode) != ESP_OK) return false;
  if (!started_) {
    if (esp_wifi_start() != ESP_OK) return false;
    started_ = true;
  }
  return true;
}

wifi_mode_t WiFiClass::getMode() const {
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK) return WIFI_MODE_NULL;
  return mode;
}

bool WiFiClass::config(const IPAddress& local_ip, const IPAddress& gateway,
                       const IPAddress& subnet, const IPAddress& dns1,
                       const IPAddress& dns2) {
  ensureInit();
  if (staNetif_ == nullptr) return false;

  esp_netif_dhcpc_stop(staNetif_);

  esp_netif_ip_info_t info{};
  info.ip = toIp4(local_ip);
  info.gw = toIp4(gateway);
  info.netmask = toIp4(subnet);
  if (esp_netif_set_ip_info(staNetif_, &info) != ESP_OK) return false;

  esp_netif_dns_info_t dns{};
  dns.ip.u_addr.ip4 = toIp4(dns1);
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  esp_netif_set_dns_info(staNetif_, ESP_NETIF_DNS_MAIN, &dns);

  dns.ip.u_addr.ip4 = toIp4(dns2);
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  esp_netif_set_dns_info(staNetif_, ESP_NETIF_DNS_BACKUP, &dns);
  return true;
}

IPAddress WiFiClass::softAPIP() const {
  if (apNetif_ == nullptr) return IPAddress();
  esp_netif_ip_info_t info{};
  if (esp_netif_get_ip_info(apNetif_, &info) != ESP_OK) return IPAddress();
  return fromIpInfo(info);
}

IPAddress WiFiClass::localIP() const {
  if (staNetif_ == nullptr) return IPAddress();
  esp_netif_ip_info_t info{};
  if (esp_netif_get_ip_info(staNetif_, &info) != ESP_OK) return IPAddress();
  return fromIpInfo(info);
}

IPAddress WiFiClass::gatewayIP() const {
  if (staNetif_ == nullptr) return IPAddress();
  esp_netif_ip_info_t info{};
  if (esp_netif_get_ip_info(staNetif_, &info) != ESP_OK) return IPAddress();
  IPAddress ip;
  ip.fromU32(ntohl(info.gw.addr));
  return ip;
}

IPAddress WiFiClass::subnetMask() const {
  if (staNetif_ == nullptr) return IPAddress();
  esp_netif_ip_info_t info{};
  if (esp_netif_get_ip_info(staNetif_, &info) != ESP_OK) return IPAddress();
  IPAddress ip;
  ip.fromU32(ntohl(info.netmask.addr));
  return ip;
}

IPAddress WiFiClass::dnsIP(uint8_t index) const {
  if (staNetif_ == nullptr) return IPAddress();
  esp_netif_dns_info_t dns{};
  esp_netif_dns_type_t type =
      index == 0 ? ESP_NETIF_DNS_MAIN : ESP_NETIF_DNS_BACKUP;
  if (esp_netif_get_dns_info(staNetif_, type, &dns) != ESP_OK) {
    return IPAddress();
  }
  if (dns.ip.type != ESP_IPADDR_TYPE_V4) return IPAddress();
  return fromIp4(dns.ip.u_addr.ip4);
}

int32_t WiFiClass::RSSI() const {
  wifi_ap_record_t ap{};
  if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
  return ap.rssi;
}

std::string WiFiClass::SSID() const {
  wifi_ap_record_t ap{};
  if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return std::string();
  return std::string(reinterpret_cast<const char*>(ap.ssid));
}

std::string WiFiClass::BSSIDstr() const {
  wifi_ap_record_t ap{};
  if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return std::string();
  char buffer[18]{};
  std::snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x",
                ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4],
                ap.bssid[5]);
  return std::string(buffer);
}

int32_t WiFiClass::channel() const {
  wifi_ap_record_t ap{};
  if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
  return ap.primary;
}

const char* WiFiClass::getHostname() const {
  const char* hostname = nullptr;
  if (staNetif_ != nullptr) {
    esp_netif_get_hostname(staNetif_, &hostname);
  }
  if (hostname != nullptr) return hostname;
  if (!hostname_.empty()) return hostname_.c_str();
  return "";
}

std::string WiFiClass::macAddress() const {
  uint8_t mac[6]{};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char buffer[18]{};
  std::snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0],
                mac[1], mac[2], mac[3], mac[4], mac[5]);
  return std::string(buffer);
}

void WiFiClass::emitEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (callback_) callback_(event, info);
}

void WiFiClass::handleWifiEvent(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
  (void)event_base;
  (void)event_data;
  WiFiClass* self = static_cast<WiFiClass*>(arg);
  if (self == nullptr) return;

  if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
    self->emitEvent(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, nullptr);
  }
}

void WiFiClass::handleIpEvent(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data) {
  (void)event_base;
  (void)event_data;
  WiFiClass* self = static_cast<WiFiClass*>(arg);
  if (self == nullptr) return;

  if (event_id == IP_EVENT_STA_GOT_IP) {
    self->emitEvent(ARDUINO_EVENT_WIFI_STA_GOT_IP, nullptr);
  }
}
