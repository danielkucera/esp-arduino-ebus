#include <catch2/catch_test_macros.hpp>
#include <string>

#include "system/string_pool.hpp"

// NOTE: StringPool is a process-wide singleton, so this is a single ordered
// test case using unique keys to stay independent of execution order.
TEST_CASE("StringPool interns and looks up strings", "[string_pool]") {
  StringPool& pool = StringPool::instance();
  const uint8_t base = pool.count();

  const uint8_t id1 = pool.intern("__test_key_alpha__");
  const uint8_t id2 = pool.intern("__test_key_beta__");
  REQUIRE(id1 != 0);
  REQUIRE(id2 != 0);
  REQUIRE(id1 != id2);

  // Interning the same string returns the existing id.
  REQUIRE(pool.intern("__test_key_alpha__") == id1);

  REQUIRE(std::string(pool.lookup(id1)) == "__test_key_alpha__");
  REQUIRE(std::string(pool.lookup(id2)) == "__test_key_beta__");
  REQUIRE(pool.count() == static_cast<uint8_t>(base + 2));

  // Empty and out-of-range lookups are safe.
  REQUIRE(pool.intern("") == 0);
  REQUIRE(pool.lookup(0).empty());
}
