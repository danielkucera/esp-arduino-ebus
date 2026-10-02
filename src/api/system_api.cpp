#include "api/system_api.hpp"

#if defined(EBUS_INTERNAL)

#if __has_include("app/build_info_gen.hpp")
#include "app/build_info_gen.hpp"
#endif

#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_heap_caps.h>
#include <esp_idf_version.h>
#include <esp_private/esp_clk.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "app/app_limits.hpp"
#include "app/cron.hpp"
#include "app/ebus_accessor.hpp"
#include "app/mqtt.hpp"
#include "network/captive_dns.hpp"
#include "network/http.hpp"
#include "network/http_utils.hpp"
#include "network/wifi_network_manager.hpp"
#include "system/adapter_version.hpp"
#include "system/device_identity.hpp"
#include "system/device_status.hpp"
#include "system/esp_ota_manager.hpp"
#include "system/logger.hpp"
#include "system/system_monitor.hpp"

SystemApi::SystemApi() {}

bool SystemApi::registerHandlers(httpd_handle_t server) {
  if (server == nullptr) return false;

  RegisterUri("/api/v1/system", HTTP_GET, handleSystem);
  RegisterUri("/api/v1/system/heap", HTTP_GET, handleHeap);
  RegisterUri("/api/v1/system/tasks", HTTP_GET, handleTasks);

  return true;
}

esp_err_t SystemApi::handleSystem(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  ebus::detail::JsonWriter writer([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  {
    auto scope = writer.objectScope();
    {
      auto firmware = writer.objectScope("firmware");
      writer.writeField("version", AUTO_VERSION);
#if __has_include("app/build_info_gen.hpp")
      writer.writeField("build", build_time_str);
#else
      writer.writeField("build", __DATE__ " " __TIME__);
#endif
      writer.writeField("esp_idf_version", esp_get_idf_version());
      writer.writeField("unique_id", getUniqueId());
      writer.writeField("adapter_hw_version", getAdapterHwVersionString());
      writer.writeField("adapter_hw_version_raw", getAdapterHwVersionRaw());
      writer.writeField("clock_speed", esp_clk_cpu_freq() / 1000000U);
      writer.writeField("apb_speed", esp_clk_apb_freq());
    }
    {
      auto chip = writer.objectScope("chip");
      esp_chip_info_t chip_info{};
      esp_chip_info(&chip_info);
      writer.writeField("chip_revision", chip_info.revision);
      uint32_t flash_size = 0;
      if (esp_flash_default_chip != nullptr &&
          esp_flash_get_size(esp_flash_default_chip, &flash_size) == ESP_OK) {
        writer.writeField("flash_size", flash_size);
      }
    }
  }
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

// Current values + hourly trend samples (leak/fragmentation instrument).
// Object shape; the page renders current plus the trend table.
esp_err_t SystemApi::handleHeap(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  ebus::detail::JsonWriter writer([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  SystemMonitor::HeapSample trend[SystemMonitor::heap_trend_capacity];
  size_t n = DeviceStatus::monitor().fetchHeapTrend(
      trend, SystemMonitor::heap_trend_capacity);
  {
    auto scope = writer.objectScope();
    {
      auto current = writer.objectScope("current");
      multi_heap_info_t info;
      heap_caps_get_info(&info, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      writer.writeField("free", info.total_free_bytes);
      writer.writeField("largest", info.largest_free_block);
      writer.writeField("min", info.minimum_free_bytes);
    }
    {
      auto arr = writer.arrayScope("trend");
      for (size_t i = 0; i < n; ++i) {
        auto item = writer.objectScope();
        writer.writeField("uptime", trend[i].uptime_seconds);
        writer.writeField("free", trend[i].free_bytes);
        writer.writeField("min", trend[i].min_bytes);
        writer.writeField("largest", trend[i].largest_block);
      }
    }
  }
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

// Per-task stacks + CPU shares (who starves the bus thread?).
// Object of two arrays; the page renders one table each.
esp_err_t SystemApi::handleTasks(httpd_req_t* req) {
  httpd_resp_set_type(req, "application/json;charset=utf-8");
  HttpUtils::applyCustomHeaders(req);
  ebus::detail::JsonWriter writer([req](std::string_view chunk) {
    httpd_resp_send_chunk(req, chunk.data(), chunk.size());
  });
  {
    auto scope = writer.objectScope();
    {
      auto threads = writer.arrayScope("threads");
      auto addThread = [&](const char* name, TaskHandle_t handle,
                           uint32_t stack_size) {
        if (!handle) return;
        auto item = writer.objectScope();
        writer.writeField("name", name);
        writer.writeField("stack_size", stack_size);
        writer.writeField(
            "stack_free",
            static_cast<uint32_t>(uxTaskGetStackHighWaterMark(handle) *
                                  sizeof(StackType_t)));
      };
      addThread("mqtt", DeviceStatus::mqtt().getTaskHandle(),
                app::limits::Task::mqtt_stack);
      addThread("cron", cron.getTaskHandle(), app::limits::Task::cron_stack);
      addThread("logger", logger.getTaskHandle(),
                app::limits::Task::logger_stack);
      addThread("dns", getCaptiveDnsTaskHandle(), app::limits::Task::dns_stack);
#if EBUS_ENABLE_OTA
      addThread("espota", DeviceStatus::espOtaManager().getTaskHandle(),
                app::limits::Task::espota_stack);
#endif
      addThread("status_led", WifiNetworkManager::getStatusLedTaskHandle(),
                app::limits::Task::status_led_stack);
      addThread("system_monitor", DeviceStatus::monitor().task_handle(),
                app::limits::Task::system_monitor_stack);
      getEbusController().fetchStatus(
          [&](const ebus::SystemResources& resources) {
            for (const auto& thread : resources.threads) {
              auto item = writer.objectScope();
              writer.writeField("name", thread.name.c_str());
              writer.writeField("stack_size", thread.stack_size);
              writer.writeField("stack_free", thread.stack_free);
            }
          });
    }
    {
      // Per-task CPU share as % of the interval since the last poll
      // (not cumulative since boot: the U32 esp_timer source wraps
      // ~every 71min, which made cumulative % grow past 100%).
      // Unsigned subtraction stays correct across the wrap. First poll
      // after boot falls back to cumulative (valid inside the first
      // wrap period); later polls report true intervals.
      auto cpu = writer.arrayScope("cpu");
      constexpr size_t max_tracked_tasks = 24;
      const UBaseType_t task_count = uxTaskGetNumberOfTasks();
      auto* states = static_cast<TaskStatus_t*>(
          heap_caps_malloc(task_count * sizeof(TaskStatus_t), MALLOC_CAP_8BIT));
      if (states != nullptr) {
        uint32_t total_time = 0;
        const UBaseType_t captured =
            uxTaskGetSystemState(states, task_count, &total_time);
        struct CpuBaseline {
          char name[configMAX_TASK_NAME_LEN];
          uint32_t last;
        };
        static portMUX_TYPE cpu_mux = portMUX_INITIALIZER_UNLOCKED;
        static CpuBaseline baseline[max_tracked_tasks]{};
        static size_t baseline_used = 0;
        static uint32_t last_total = 0;
        static bool have_total = false;
        // Computed under one short critical section (no JSON inside),
        // then emitted below without the lock held.
        struct CpuRow {
          char name[configMAX_TASK_NAME_LEN];
          float pct;
          uint32_t prio;
        };
        CpuRow rows[max_tracked_tasks]{};
        size_t nrows = 0;
        portENTER_CRITICAL(&cpu_mux);
        const uint32_t delta_total = total_time - last_total;
        for (UBaseType_t i = 0; i < captured && nrows < max_tracked_tasks;
             ++i) {
          size_t bi = baseline_used;
          for (size_t b = 0; b < baseline_used; ++b) {
            if (std::strncmp(baseline[b].name, states[i].pcTaskName,
                             configMAX_TASK_NAME_LEN) == 0) {
              bi = b;
              break;
            }
          }
          float pct = 0.0f;
          if (bi < baseline_used && have_total && delta_total > 0) {
            pct = (100.0f * (states[i].ulRunTimeCounter - baseline[bi].last)) /
                  delta_total;
          } else if (!have_total && total_time > 0) {
            pct = (100.0f * states[i].ulRunTimeCounter) / total_time;
          }
          if (bi == baseline_used) {
            if (baseline_used >= max_tracked_tasks) continue;
            std::strncpy(baseline[bi].name, states[i].pcTaskName,
                         configMAX_TASK_NAME_LEN - 1);
            baseline[bi].name[configMAX_TASK_NAME_LEN - 1] = '\0';
            baseline_used++;
          }
          baseline[bi].last = states[i].ulRunTimeCounter;
          std::strncpy(rows[nrows].name, states[i].pcTaskName,
                       configMAX_TASK_NAME_LEN - 1);
          rows[nrows].name[configMAX_TASK_NAME_LEN - 1] = '\0';
          rows[nrows].pct = pct;
          rows[nrows].prio = static_cast<uint32_t>(states[i].uxCurrentPriority);
          nrows++;
        }
        last_total = total_time;
        have_total = true;
        portEXIT_CRITICAL(&cpu_mux);
        for (size_t i = 0; i < nrows; ++i) {
          auto item = writer.objectScope();
          writer.writeField("name", rows[i].name);
          writer.writeFieldFloat("percent", rows[i].pct);
          writer.writeField("priority", rows[i].prio);
        }
        heap_caps_free(states);
      }
    }
  }
  httpd_resp_send_chunk(req, nullptr, 0);
  return ESP_OK;
}

#endif
