#include "app/mqtt_endpoint.hpp"

#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {
struct Case {
  std::string input;
  std::string expected_uri;  // Empty means invalid.
  bool tls;
  unsigned port = 1883;
};
void check(bool ok, const std::string& message) {
  if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}  // namespace

int main() {
  const std::vector<Case> cases = {
    {"vicktor-ha.duckdns.org", "mqtt://vicktor-ha.duckdns.org:1883", false},
    {"192.168.1.28", "mqtt://192.168.1.28:1883", false},
    {"  broker.local\r\n", "mqtt://broker.local:1883", false},
    {"broker.local", "mqtt://broker.local:1884", false, 1884},
    {"broker.local", "mqtt://broker.local:1883", false, 0},
    {"broker.local:1885", "mqtt://broker.local:1885", false},
    {"mqtt://broker.local", "mqtt://broker.local", false},
    {"mqtt://broker.local:1884", "mqtt://broker.local:1884", false},
    {"mqtt://broker.local/", "mqtt://broker.local/", false},
    {"mqtts://broker.local", "mqtts://broker.local", true},
    {"mqtts://broker.local:8883", "mqtts://broker.local:8883", true},
    {"ws://broker.local", "ws://broker.local/mqtt", false},
    {"ws://broker.local:8083/mqtt", "ws://broker.local:8083/mqtt", false},
    {"wss://mqtt.vicktor-ha.duckdns.org:443/mqtt", "wss://mqtt.vicktor-ha.duckdns.org:443/mqtt", true},
    {"wss://broker.local", "wss://broker.local/mqtt", true},
    {"WSS://broker.local:443/mqtt", "wss://broker.local:443/mqtt", true},
    {"wss://broker.local:444/other", "wss://broker.local:444/other", true},
    {"wss://broker.local/", "wss://broker.local/", true},
    {"wss://broker.local/a%20b", "wss://broker.local/a%20b", true},
    {"[2001:db8::1]", "mqtt://[2001:db8::1]:1883", false},
    {"wss://[2001:db8::1]:443/mqtt", "wss://[2001:db8::1]:443/mqtt", true},
    {"", "", false}, {" \t\n", "", false},
    {"https://broker.local", "", false}, {"wss:/broker.local", "", false},
    {"wss://", "", false}, {"wss:///mqtt", "", false},
    {"wss://broker.local:0/mqtt", "", false},
    {"wss://broker.local:65536/mqtt", "", false},
    {"wss://broker.local:/mqtt", "", false},
    {"wss://broker.local:443x/mqtt", "", false},
    {"wss://broker.local:443/mqtt:1883", "wss://broker.local:443/mqtt:1883", true},
    {"wss://user:secret@broker.local/mqtt", "", false},
    {"user:secret@broker.local", "", false},
    {"wss://broker.local/mqtt?token=secret", "", false},
    {"wss://broker.local/mqtt#fragment", "", false},
    {"wss://bro ker.local/mqtt", "", false},
    {"wss://broker.local/\rmqtt", "", false},
    {"wss://broker.local\\mqtt", "", false},
    {"mqtt://broker.local/mqtt", "", false},
    {"wss://[2001:db8::1:443/mqtt", "", false},
    {"wss://[]/mqtt", "", false}, {"wss://[broker]/mqtt", "", false},
    {"wss://[2001:db8::1]oops/mqtt", "", false},
    {"wss://[2001:db8::zz]/mqtt", "", false},
    {"2001:db8::1", "", false},
    {std::string(501, 'a'), "", false},
    {std::string("wss://broker\0evil/mqtt", 22), "", false},
  };
  for (std::size_t i = 0; i < cases.size(); ++i) {
    const auto& c = cases[i];
    auto e = mqtt_endpoint::parse(c.input, static_cast<uint16_t>(c.port));
    const auto where = "case " + std::to_string(i);
    check(static_cast<bool>(e) == !c.expected_uri.empty(), where + " validity");
    check(e.uri == c.expected_uri, where + " URI");
    if (e) check(e.tls == c.tls, where + " TLS selection");
    else check(e.error != nullptr && e.uri.empty(), where + " fail-closed");
  }
  // Exercise control bytes and random malformed inputs under ASan/UBSan locally.
  std::mt19937 random(9142026);
  for (unsigned i = 0; i < 5000; ++i) {
    std::string text;
    const unsigned size = random() % 160;
    for (unsigned j = 0; j < size; ++j) text += static_cast<char>(random() & 0xff);
    auto e = mqtt_endpoint::parse(text);
    if (e) {
      check(e.uri.find("://") != std::string::npos, "fuzz scheme");
      check((e.uri.rfind("wss://", 0) == 0 || e.uri.rfind("mqtts://", 0) == 0) == e.tls,
            "fuzz secure transport consistency");
    } else check(e.uri.empty(), "invalid input must not produce a fallback URI");
  }
  std::cout << "PASS: " << cases.size() << " endpoint cases + 5000 malformed-input cases\n";
}
