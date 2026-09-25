#include "web_admin_policy.hpp"

#include <cassert>
#include <iostream>
#include <string>

int main() {
  using namespace web_admin;
  assert(constantTimeEqual("Basic example", "Basic example"));
  assert(!constantTimeEqual("Basic example", "Basic examplf"));
  assert(!constantTimeEqual("Basic example", ""));
  for (const auto key : {"wifiPassword", "mqttPass", "apModePassword"}) {
    assert(isSensitiveKey(key));
    assert(preserveSecret(key, "********"));
    assert(!preserveSecret(key, ""));
    assert(!preserveSecret(key, "replacement-example"));
  }
  assert(!preserveSecret("thingName", "********"));
  assert(!isSensitiveKey("wifiSsid"));
  assert(sameOrigin("", ""));
  assert(sameOrigin("http://adapter.test", "adapter.test"));
  assert(sameOrigin("http://adapter.test:8080", "adapter.test:8080"));
  assert(!sameOrigin("null", "adapter.test"));
  assert(!sameOrigin("https://adapter.test", "adapter.test"));
  assert(!sameOrigin("http://adapter.test.evil", "adapter.test"));
  assert(!sameOrigin("http://adapter.test/path", "adapter.test"));
  assert(!sameOrigin("http://adapter.test", ""));
  assert(!sameOrigin("http://" + std::string(128, 'a'), std::string(128, 'a')));
  std::cout << "Web administration policy tests passed\n";
}
