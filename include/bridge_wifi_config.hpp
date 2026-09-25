#pragma once

#include <algorithm>

// The bridge fans out a continuous stream of small TCP packets. Do not inherit
// the internal application's reduced 4/4/2 RX/dynamic-RX/TX memory budget.
// These floors match ESP-IDF's normal non-PSRAM Wi-Fi buffer defaults.
template <typename WifiConfig>
void applyBridgeWifiBuffers(WifiConfig& config) {
#if !defined(EBUS_INTERNAL)
  config.static_rx_buf_num = std::max(config.static_rx_buf_num, 10);
  if (config.dynamic_rx_buf_num != 0)  // zero means unlimited
    config.dynamic_rx_buf_num = std::max(config.dynamic_rx_buf_num, 32);
  if (config.tx_buf_type == 0)
    config.static_tx_buf_num = std::max(config.static_tx_buf_num, 16);
  if (config.rx_ba_win != 0)
    config.rx_ba_win = std::max(config.rx_ba_win, 6);
#else
  (void)config;
#endif
}
