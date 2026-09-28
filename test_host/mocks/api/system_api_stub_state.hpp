#pragma once

#include "system/system_monitor.hpp"

namespace HostSystemApiStub {

void setHeapTrend(const SystemMonitor::HeapSample* samples, size_t count);
void clearHeapTrend();
void resetTap();
uint64_t tapSinceMillis();

}  // namespace HostSystemApiStub
