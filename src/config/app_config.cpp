#include "config/app_config.hpp"

#include <ebus/detail/json_reader.hpp>
#include <ebus/detail/json_writer.hpp>
#include <ebus/types.hpp>
#include <string_view>

void AppConfig::reset() {
  *this = AppConfig{};
  network.wifi_ssid = "ebus-test";
  network.wifi_password = "lectronz";
  network.ap_password = "ebusebus";
  sntp.server = "pool.ntp.org";
  sntp.timezone = "UTC0";
  bus.address = "ff";
  mqtt_ha.thing_name = "esp-eBus";
  pwm.value = 130;
  bus.window_us = 4400;
  bus.offset_us = 50;
  bus.system_inquiry = false;
  bus.system_response = true;
  bus.scan_on_startup = false;
}

void AppConfig::toJson(ebus::detail::JsonWriter& writer) const {
  auto scope = writer.objectScope();

  {
    auto netScope = writer.objectScope("network");
    writer.writeField("wifi_ssid", network.wifi_ssid.c_str());
    writer.writeField("wifi_password", network.wifi_password.c_str());
    writer.writeField("wifi_bssid", network.wifi_bssid.c_str());
    writer.writeField("ap_password", network.ap_password.c_str());

    writer.writeField("static_ip_enabled", network.static_ip_enabled);
    writer.writeField("ip_address", network.ip_address.c_str());
    writer.writeField("gateway", network.gateway.c_str());
    writer.writeField("netmask", network.netmask.c_str());
    writer.writeField("dns1", network.dns1.c_str());
    writer.writeField("dns2", network.dns2.c_str());
  }

  {
    auto sntpScope = writer.objectScope("sntp");
    writer.writeField("enabled", sntp.enabled);
    writer.writeField("server", sntp.server.c_str());
    writer.writeField("timezone", sntp.timezone.c_str());
  }

  writer.writeField("pwm", pwm.value);

  {
    auto busScope = writer.objectScope("bus");
    writer.writeField("window_us", bus.window_us);
    writer.writeField("offset_us", bus.offset_us);
    writer.writeField("address", bus.address.c_str());
    writer.writeField("system_inquiry", bus.system_inquiry);
    writer.writeField("system_response", bus.system_response);
    writer.writeField("scan_on_startup", bus.scan_on_startup);
  }

  {
    auto mqttScope = writer.objectScope("mqtt");
    writer.writeField("enabled", mqtt.enabled);
    writer.writeField("server", mqtt.server.c_str());
    writer.writeField("user", mqtt.user.c_str());
    writer.writeField("pass", mqtt.pass.c_str());
    writer.writeField("root_topic", mqtt.root_topic.c_str());
  }

  {
    auto mqttHaScope = writer.objectScope("mqtt_ha");
    writer.writeField("enabled", mqtt_ha.enabled);
    writer.writeField("thing_name", mqtt_ha.thing_name.c_str());
  }

  writer.writeField("http_headers", http.headers.c_str());
}

AppConfig AppConfig::fromJson(std::string_view json) {
  AppConfig cfg;
  cfg.mergeFromJson(json);
  return cfg;
}

bool AppConfig::mergeFromJson(std::string_view json) {
  ebus::detail::JsonReader reader(json);
  if (reader.next() != ebus::detail::JsonReader::Token::object_start)
    return false;

  reader.forEachField([&](std::string_view key, ebus::detail::JsonReader& r) {
    if (key == "network") {
      if (r.next() == ebus::detail::JsonReader::Token::object_start) {
        r.forEachField(
            [&](std::string_view k, ebus::detail::JsonReader& inner) {
              if (k == "wifi_ssid") {
                inner.next();
                network.wifi_ssid.assign(inner.value());
                return true;
              }
              if (k == "wifi_password") {
                inner.next();
                network.wifi_password.assign(inner.value());
                return true;
              }
              if (k == "wifi_bssid") {
                inner.next();
                network.wifi_bssid.assign(inner.value());
                return true;
              }
              if (k == "ap_password") {
                inner.next();
                network.ap_password.assign(inner.value());
                return true;
              }

              if (k == "static_ip_enabled") {
                inner.next();
                network.static_ip_enabled = inner.asBool();
                return true;
              }
              if (k == "ip_address") {
                inner.next();
                network.ip_address.assign(inner.value());
                return true;
              }
              if (k == "gateway") {
                inner.next();
                network.gateway.assign(inner.value());
                return true;
              }
              if (k == "netmask") {
                inner.next();
                network.netmask.assign(inner.value());
                return true;
              }
              if (k == "dns1") {
                inner.next();
                network.dns1.assign(inner.value());
                return true;
              }
              if (k == "dns2") {
                inner.next();
                network.dns2.assign(inner.value());
                return true;
              }
              return false;
            });
      }
      return true;
    }

    if (key == "sntp") {
      if (r.next() == ebus::detail::JsonReader::Token::object_start) {
        r.forEachField(
            [&](std::string_view k, ebus::detail::JsonReader& inner) {
              if (k == "enabled") {
                inner.next();
                sntp.enabled = inner.asBool();
                return true;
              }
              if (k == "server") {
                inner.next();
                sntp.server.assign(inner.value());
                return true;
              }
              if (k == "timezone") {
                inner.next();
                sntp.timezone.assign(inner.value());
                return true;
              }
              return false;
            });
      }
      return true;
    }

    if (key == "pwm") {
      r.next();
      auto val = r.asNumStrict<uint8_t>();
      if (val) pwm.value = *val;
      return val.has_value();
    }

    if (key == "bus") {
      if (r.next() == ebus::detail::JsonReader::Token::object_start) {
        r.forEachField(
            [&](std::string_view k, ebus::detail::JsonReader& inner) {
              if (k == "window_us") {
                inner.next();
                auto val = inner.asNumStrict<uint16_t>();
                if (val) bus.window_us = *val;
                return val.has_value();
              }
              if (k == "offset_us") {
                inner.next();
                auto val = inner.asNumStrict<uint16_t>();
                if (val) bus.offset_us = *val;
                return val.has_value();
              }
              if (k == "address") {
                inner.next();
                bus.address.assign(inner.value());
                return true;
              }
              if (k == "system_inquiry") {
                inner.next();
                bus.system_inquiry = inner.asBool();
                return true;
              }
              if (k == "system_response") {
                inner.next();
                bus.system_response = inner.asBool();
                return true;
              }
              if (k == "scan_on_startup") {
                inner.next();
                bus.scan_on_startup = inner.asBool();
                return true;
              }
              return false;
            });
      }
      return true;
    }

    if (key == "mqtt") {
      if (r.next() == ebus::detail::JsonReader::Token::object_start) {
        r.forEachField(
            [&](std::string_view k, ebus::detail::JsonReader& inner) {
              if (k == "enabled") {
                inner.next();
                mqtt.enabled = inner.asBool();
                return true;
              }
              if (k == "server") {
                inner.next();
                mqtt.server.assign(inner.value());
                return true;
              }
              if (k == "user") {
                inner.next();
                mqtt.user.assign(inner.value());
                return true;
              }
              if (k == "pass") {
                inner.next();
                mqtt.pass.assign(inner.value());
                return true;
              }
              if (k == "root_topic") {
                inner.next();
                mqtt.root_topic.assign(inner.value());
                return true;
              }
              return false;
            });
      }
      return true;
    }

    if (key == "mqtt_ha") {
      if (r.next() == ebus::detail::JsonReader::Token::object_start) {
        r.forEachField(
            [&](std::string_view k, ebus::detail::JsonReader& inner) {
              if (k == "enabled") {
                inner.next();
                mqtt_ha.enabled = inner.asBool();
                return true;
              }
              if (k == "thing_name") {
                inner.next();
                mqtt_ha.thing_name.assign(inner.value());
                return true;
              }
              return false;
            });
      }
      return true;
    }

    if (key == "http_headers") {
      r.next();
      http.headers.assign(r.value());
      return true;
    }
    return false;
  });

  return true;
}

bool AppConfig::isValidJson(std::string_view json) {
  return ebus::detail::JsonReader::validate(json);
}