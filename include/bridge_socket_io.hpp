#pragma once

#include <lwip/sockets.h>

#include <cerrno>
#include <atomic>
#include <cstdint>

namespace bridge {

// FIONREAD is optional in lwIP (requires SO_RCVBUF). A nonblocking peek works
// with the lean ESP-IDF configuration too, and does not consume the stream.
inline bool socketHasInput(int fd) {
  if (fd < 0) return false;
  uint8_t byte;
  return recv(fd, &byte, 1, MSG_PEEK | MSG_DONTWAIT) > 0;
}

enum class CommandRead { ready, incomplete, invalid, closed };

class EnhancedCommandReader {
 public:
  void reset() { prefix_ = -1; }

  CommandRead read(int fd, uint8_t (&command)[2]) {
    for (;;) {
      uint8_t byte;
      const int count = recv(fd, &byte, 1, MSG_DONTWAIT);
      if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
        return CommandRead::incomplete;
      if (count <= 0) {
        reset();
        return CommandRead::closed;
      }
      const int prefix = prefix_.load();
      if (prefix < 0) {
        if (byte < 0x80) {
          command[0] = 1;
          command[1] = byte;
          return CommandRead::ready;
        }
        if ((byte & 0xc0) != 0xc0) return CommandRead::invalid;
        // TCP may split at any boundary. Retain this prefix for the next call;
        // consuming it also lets a truncated EOF be detected without spinning.
        prefix_ = byte;
        continue;
      }
      reset();
      if ((byte & 0xc0) != 0x80) return CommandRead::invalid;
      command[0] = (prefix >> 2) & 0x0f;
      command[1] = ((prefix & 0x03) << 6) | (byte & 0x3f);
      return CommandRead::ready;
    }
  }

 private:
  // The accept task resets state before publishing a replacement connection.
  std::atomic<int> prefix_{-1};
};

}  // namespace bridge
