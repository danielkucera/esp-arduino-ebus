#if defined(EBUS_INTERNAL)

#include "network/sntp.hpp"

#include <esp_sntp.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "system/logger.hpp"

#define DEFAULT_SNTP_SERVER "pool.ntp.org"

namespace {

void timeSyncNotificationCallback(struct timeval* tv) {
  (void)tv;
  char buf[128];
  const char* active_server =
      esp_sntp_getservername(0);  // This can return NULL
  snprintf(buf, sizeof(buf), "SNTP synchronized to %s",
           (active_server != nullptr ? active_server : "unknown"));
  logger.info(buf);
}

std::string sntp_server_storage = DEFAULT_SNTP_SERVER;

}  // namespace

void initSntp(const AppConfig::Sntp& sntp) {
  if (!sntp.server.empty()) {
    sntp_server_storage = sntp.server.c_str();
  } else {
    sntp_server_storage = DEFAULT_SNTP_SERVER;
  }

  sntp_set_sync_interval(1 * 60 * 60 * 1000UL);  // 1 hour

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(
      0, sntp_server_storage.c_str());  // This expects a non-null c_str()

  sntp_set_time_sync_notification_cb(timeSyncNotificationCallback);
  esp_sntp_init();
  char buf[128];
  snprintf(buf, sizeof(buf), "SNTP started with server %s",
           sntp_server_storage.c_str());
  logger.info(buf);
}

void setTimezone(const AppConfig::Sntp& sntp) {
  if (!sntp.timezone.empty()) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Timezone set to %s", sntp.timezone.c_str());
    logger.info(buf);
    setenv("TZ", sntp.timezone.c_str(), 1);
    tzset();
  }
}

void appendSntpStatus(ebus::detail::JsonWriter& writer,
                      const AppConfig::Sntp& sntp) {
  writer.writeField("enabled", sntp.enabled);
  const char* active_sntp_server = esp_sntp_getservername(0);
  if (active_sntp_server != nullptr) {
    writer.writeField("server", active_sntp_server);
  } else {
    writer.writeField("server", sntp.server.c_str());
  }
  writer.writeField("timezone", sntp.timezone.c_str());
}

#endif
