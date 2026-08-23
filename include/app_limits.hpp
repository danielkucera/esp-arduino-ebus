/*
 * Copyright (C) 2026 Roland Jax
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace app::limits {

// --- Task Orchestration ---
namespace Task {
inline constexpr uint32_t mqtt_stack = 7168;
inline constexpr uint8_t mqtt_priority = 3;
inline constexpr uint32_t cron_stack = 1536;
inline constexpr uint8_t cron_priority = 2;
inline constexpr uint32_t logger_stack = 3072;
inline constexpr uint8_t logger_priority = 1;
inline constexpr uint32_t dns_stack = 2048;
inline constexpr uint8_t dns_priority = 1;
inline constexpr uint32_t espota_stack = 8192;
inline constexpr uint8_t espota_priority = 1;
inline constexpr uint32_t status_led_stack = 1024;
inline constexpr uint8_t status_led_priority = 1;
inline constexpr uint32_t system_monitor_stack = 3072;
inline constexpr uint8_t system_monitor_priority = 1;
inline constexpr uint32_t data_loop_stack = 10000;
inline constexpr uint8_t data_loop_priority = 1;
inline constexpr uint32_t client_accept_stack = 4096;
inline constexpr uint8_t client_accept_priority = 1;
}  // namespace Task

// --- eBUS Runtime Defaults ---
namespace Ebus {
inline constexpr uint32_t window_us = 4300;
inline constexpr uint32_t offset_us = 80;
inline constexpr uint32_t watchdog_timeout_ms = 250;
}  // namespace Ebus

// --- Network ---
namespace Network {
inline constexpr uint16_t mqtt_port = 1883;
inline constexpr uint16_t tcp_port_regular = 3333;
inline constexpr uint16_t tcp_port_readonly = 3334;
inline constexpr uint16_t tcp_port_enhanced = 3335;
inline constexpr uint16_t captive_dns_port = 53;
inline constexpr uint32_t session_timeout_ms = 2000;
inline constexpr uint32_t transmit_timeout_ms = 1000;
inline constexpr uint32_t outbound_buffer_size = 2048;
inline constexpr uint16_t http_port = 80;
inline constexpr uint16_t mdns_http_port = 80;
inline constexpr uint32_t http_client_timeout_ms = 20000;
}  // namespace Network

// --- HTTP Server ---
namespace Http {
inline constexpr uint32_t stack_size = 8192;
inline constexpr size_t max_uri_handlers = 64;
inline constexpr size_t max_open_sockets = 2;
inline constexpr uint32_t recv_wait_timeout = 10;
inline constexpr uint32_t send_wait_timeout = 10;
inline constexpr size_t max_request_body_size = 8192;
inline constexpr size_t streaming_buffer_size = 4096;
}  // namespace Http

// --- MQTT ---
namespace Mqtt {
inline constexpr uint32_t buffer_size = 1536;
inline constexpr uint32_t out_buffer_size = 1536;
inline constexpr uint32_t topic_buffer_size = 256;
inline constexpr uint32_t keepalive_s = 60;
inline constexpr uint32_t status_publish_interval_ms = 10000;
inline constexpr size_t max_outgoing_queue = 8;
}  // namespace Mqtt

// --- Buffers ---
namespace Buffer {
inline constexpr size_t mqtt_full_topic = 256;
inline constexpr size_t http_query_string = 256;
inline constexpr size_t http_query_value = 32;
inline constexpr size_t http_upload = 512;
inline constexpr size_t json_reader = 1536;
inline constexpr size_t json_row = 1024;
inline constexpr size_t json_chunk = 512;
inline constexpr size_t ota_buffer = 1024;
inline constexpr size_t ota_packet = 192;
inline constexpr size_t dns_packet = 512;
inline constexpr size_t uart_rx = 256;
inline constexpr size_t uart_tx = 256;
}  // namespace Buffer

// --- Timeouts ---
namespace Timeout {
inline constexpr uint32_t mqtt_stop_delay_ms = 50;
inline constexpr uint32_t mqtt_disabled_delay_ms = 100;
inline constexpr uint32_t cron_loop_delay_ms = 1000;
inline constexpr uint32_t dns_loop_delay_ms = 10;
inline constexpr uint32_t dns_stop_delay_ms = 20;
inline constexpr uint32_t ota_transfer_timeout_ms = 60000;
inline constexpr uint32_t ota_task_delay_ms = 10;
inline constexpr uint32_t ota_transfer_restart_delay_ms = 1000;
inline constexpr uint32_t restart_handler_delay_ms = 500;
inline constexpr uint32_t upgrade_send_restart_delay_ms = 1000;
inline constexpr uint32_t system_monitor_period_ms = 30000;
inline constexpr uint32_t log_summary_interval_ms = 300000;
inline constexpr uint32_t sntp_sync_interval_ms = 3600000;
}  // namespace Timeout

// --- Scheduler ---
namespace Scheduler {
inline constexpr uint32_t max_attempts = 1;
inline constexpr uint32_t base_backoff_ms = 100;
inline constexpr uint32_t fsm_timeout_ms = 1000;
inline constexpr uint32_t total_timeout_ms = 2000;
}  // namespace Scheduler

// --- Device ---
namespace Device {
inline constexpr uint32_t initial_delay_s = 5;
inline constexpr uint32_t startup_interval_s = 25;
inline constexpr uint32_t max_startup_scans = 5;
}  // namespace Device

// --- Capacities ---
inline constexpr size_t matching_commands_capacity = 16;

}  // namespace app::limits
