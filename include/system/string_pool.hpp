#pragma once

#if defined(EBUS_INTERNAL)

#include <cstdint>
#include <cstring>
#include <string_view>

class StringPool {
 public:
  static StringPool& instance();

  uint8_t intern(std::string_view str) {
    if (str.empty() || count_ >= max_strings_) return 0;

    for (uint8_t i = 0; i < count_; i++) {
      if (lengths_[i] == str.size() &&
          std::memcmp(buffer_ + offsets_[i], str.data(), str.size()) == 0) {
        return i + 1;
      }
    }

    if (buffer_offset_ + str.size() + 1 > buffer_size_) return 0;

    std::memcpy(buffer_ + buffer_offset_, str.data(), str.size());
    buffer_[buffer_offset_ + str.size()] = '\0';
    offsets_[count_] = buffer_offset_;
    lengths_[count_] = str.size();
    buffer_offset_ += str.size() + 1;
    return static_cast<uint8_t>(count_++ + 1);
  }

  std::string_view lookup(uint8_t id) const {
    if (id == 0 || id > count_) return {};
    const uint8_t idx = id - 1;
    return std::string_view(buffer_ + offsets_[idx], lengths_[idx]);
  }

  uint8_t count() const { return count_; }

 private:
  static constexpr size_t max_strings_ = 256;
  static constexpr size_t buffer_size_ = 2048;

  char buffer_[buffer_size_]{};
  uint16_t offsets_[max_strings_]{};
  uint8_t lengths_[max_strings_]{};
  uint8_t count_ = 0;
  uint16_t buffer_offset_ = 0;
};

#endif
