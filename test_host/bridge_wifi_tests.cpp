#include "bridge_wifi_config.hpp"

#include <cassert>
#include <cstdio>

struct WifiConfig {
  int static_rx_buf_num;
  int dynamic_rx_buf_num;
  int tx_buf_type;
  int static_tx_buf_num;
  int rx_ba_win;
  int unrelated;
};

int main() {
  WifiConfig reduced{4, 4, 0, 2, 4, 123};
  applyBridgeWifiBuffers(reduced);
#if defined(EBUS_INTERNAL)
  assert(reduced.static_rx_buf_num == 4);
  assert(reduced.dynamic_rx_buf_num == 4);
  assert(reduced.static_tx_buf_num == 2);
  assert(reduced.rx_ba_win == 4);
#else
  assert(reduced.static_rx_buf_num == 10);
  assert(reduced.dynamic_rx_buf_num == 32);
  assert(reduced.static_tx_buf_num == 16);
  assert(reduced.rx_ba_win == 6);
#endif
  assert(reduced.tx_buf_type == 0 && reduced.unrelated == 123);
  WifiConfig larger{20, 64, 0, 32, 16, 456};
  applyBridgeWifiBuffers(larger);
  assert(larger.static_rx_buf_num == 20 && larger.dynamic_rx_buf_num == 64);
  assert(larger.static_tx_buf_num == 32 && larger.rx_ba_win == 16);
  assert(larger.unrelated == 456);
  WifiConfig dynamic{10, 0, 1, 0, 0, 789};
  applyBridgeWifiBuffers(dynamic);
  assert(dynamic.dynamic_rx_buf_num == 0 && dynamic.tx_buf_type == 1);
  assert(dynamic.static_tx_buf_num == 0 && dynamic.rx_ba_win == 0);
  assert(dynamic.unrelated == 789);
  puts("Wi-Fi buffer policy tests passed");
}
