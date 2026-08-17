#pragma once

// Mock Mqtt for host testing - no ESP-IDF dependency

#include <string>
#include <string_view>

class Mqtt {
 public:
  static void publishComponentDiscovery() {}
  static void publishValue(std::string_view) {}
  static void publishError(int) {}
};
