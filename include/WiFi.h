#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include "IPAddress.h"

#ifndef WIFI_AP_STA
#define WIFI_AP_STA WIFI_MODE_APSTA
#endif

using WiFiEvent_t = int32_t;
using WiFiEventInfo_t = void*;

constexpr WiFiEvent_t ARDUINO_EVENT_WIFI_STA_GOT_IP = 1000;
constexpr WiFiEvent_t ARDUINO_EVENT_WIFI_STA_DISCONNECTED = 1001;

class WiFiClass {
 public:
  using EventCallback = std::function<void(WiFiEvent_t, WiFiEventInfo_t)>;

  WiFiClass();

  void onEvent(EventCallback cb);
  void persistent(bool enable);
  void setAutoReconnect(bool enable);
  bool setHostname(const char* hostname);
  bool mode(wifi_mode_t mode);
  wifi_mode_t getMode() const;
  bool config(const IPAddress& local_ip, const IPAddress& gateway,
              const IPAddress& subnet, const IPAddress& dns1,
              const IPAddress& dns2);

  IPAddress softAPIP() const;
  IPAddress localIP() const;
  IPAddress gatewayIP() const;
  IPAddress subnetMask() const;
  IPAddress dnsIP(uint8_t index) const;

  int32_t RSSI() const;
  std::string SSID() const;
  std::string BSSIDstr() const;
  int32_t channel() const;
  const char* getHostname() const;
  std::string macAddress() const;

 private:
  void ensureInit();
  void emitEvent(WiFiEvent_t event, WiFiEventInfo_t info);
  static void handleWifiEvent(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data);
  static void handleIpEvent(void* arg, esp_event_base_t event_base,
                            int32_t event_id, void* event_data);

  EventCallback callback_;
  esp_netif_t* staNetif_ = nullptr;
  esp_netif_t* apNetif_ = nullptr;
  bool initialized_ = false;
  bool started_ = false;
  std::string hostname_;
};

extern WiFiClass WiFi;
