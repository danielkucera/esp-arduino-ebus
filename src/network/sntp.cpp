#if defined(EBUS_INTERNAL)

#include "network/sntp.hpp"

#include <esp_sntp.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "system/logger.hpp"

#define DEFAULT_SNTP_SERVER "pool.ntp.org"

namespace {

void time_sync_notification_cb(struct timeval* tv) {
  (void)tv;
  char buf[128];
  const char* activeServer = esp_sntp_getservername(0);  // This can return NULL
  snprintf(buf, sizeof(buf), "SNTP synchronized to %s",
           (activeServer != nullptr ? activeServer : "unknown"));
  logger.info(buf);
}

std::string sntpServerStorage = DEFAULT_SNTP_SERVER;

}  // namespace

void initSNTP(const AppConfig::Sntp& sntp) {
  if (!sntp.server.empty()) {
    sntpServerStorage = sntp.server.c_str();
  } else {
    sntpServerStorage = DEFAULT_SNTP_SERVER;
  }

  sntp_set_sync_interval(1 * 60 * 60 * 1000UL);  // 1 hour

  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(
      0, sntpServerStorage.c_str());  // This expects a non-null c_str()

  sntp_set_time_sync_notification_cb(time_sync_notification_cb);
  esp_sntp_init();
  char buf[128];
  snprintf(buf, sizeof(buf), "SNTP started with server %s",
           sntpServerStorage.c_str());
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
  const char* activeSntpServer = esp_sntp_getservername(0);
  if (activeSntpServer != nullptr) {
    writer.writeField("server", activeSntpServer);
  } else {
    writer.writeField("server", sntp.server.c_str());
  }
  writer.writeField("timezone", sntp.timezone.c_str());
}

#endif
