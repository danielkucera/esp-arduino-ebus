#if defined(EBUS_INTERNAL)
#include "string_pool.hpp"

StringPool& StringPool::instance() {
  static StringPool pool;
  return pool;
}

#endif
