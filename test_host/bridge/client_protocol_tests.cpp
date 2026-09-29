#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>

void decode(int first, int second, uint8_t (&data)[2]);
void encode(uint8_t command, uint8_t value, uint8_t (&frame)[2]);
bool readCmd(int* client_fd, uint8_t (&data)[2]);

namespace {
class SocketPair {
 public:
  SocketPair() { REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, fds_) == 0); }
  ~SocketPair() {
    if (fds_[0] >= 0) close(fds_[0]);
    if (fds_[1] >= 0) close(fds_[1]);
  }

  int& client() { return fds_[0]; }
  int peer() const { return fds_[1]; }

 private:
  int fds_[2] = {-1, -1};
};

void writeBytes(int fd, const uint8_t* bytes, size_t size) {
  REQUIRE(::write(fd, bytes, size) == static_cast<ssize_t>(size));
}

std::string readPeerMessage(int fd) {
  char buffer[64]{};
  const ssize_t count = ::read(fd, buffer, sizeof(buffer));
  REQUIRE(count >= 0);
  return std::string(buffer, static_cast<size_t>(count));
}
}  // namespace

TEST_CASE("Enhanced bridge frames encode and decode command/value boundaries",
          "[bridge][client_protocol]") {
  const std::array<std::array<uint8_t, 2>, 5> cases{{
      {{0x00, 0x00}},
      {{0x01, 0x3F}},
      {{0x07, 0x40}},
      {{0x0A, 0xAA}},
      {{0x0F, 0xFF}},
  }};

  for (const auto& input : cases) {
    uint8_t frame[2]{};
    encode(input[0], input[1], frame);
    REQUIRE((frame[0] & 0xC0) == 0xC0);
    REQUIRE((frame[1] & 0xC0) == 0x80);

    uint8_t decoded[2]{};
    decode(frame[0], frame[1], decoded);
    REQUIRE(decoded[0] == input[0]);
    REQUIRE(decoded[1] == input[1]);
  }
}

TEST_CASE("Enhanced bridge command parser accepts raw and encoded commands",
          "[bridge][client_protocol]") {
  SECTION("raw byte maps to CMD_SEND") {
    SocketPair sockets;
    const uint8_t byte = 0x55;
    writeBytes(sockets.peer(), &byte, 1);

    uint8_t command[2]{};
    REQUIRE(readCmd(&sockets.client(), command));
    REQUIRE(command[0] == 1);
    REQUIRE(command[1] == byte);
  }

  SECTION("well-formed two-byte command decodes") {
    SocketPair sockets;
    uint8_t frame[2]{};
    encode(0x0B, 0xD2, frame);
    writeBytes(sockets.peer(), frame, sizeof(frame));

    uint8_t command[2]{};
    REQUIRE(readCmd(&sockets.client(), command));
    REQUIRE(command[0] == 0x0B);
    REQUIRE(command[1] == 0xD2);
  }
}

TEST_CASE(
    "Enhanced bridge command parser rejects malformed and truncated frames",
    "[bridge][client_protocol]") {
  SECTION("first byte has an invalid signature") {
    SocketPair sockets;
    const uint8_t frame[] = {0x80};
    writeBytes(sockets.peer(), frame, sizeof(frame));

    uint8_t command[2]{};
    REQUIRE_FALSE(readCmd(&sockets.client(), command));
    REQUIRE(sockets.client() == -1);
    REQUIRE(readPeerMessage(sockets.peer()) == "first command signature error");
  }

  SECTION("second byte has an invalid signature") {
    SocketPair sockets;
    const uint8_t frame[] = {0xC4, 0x40};
    writeBytes(sockets.peer(), frame, sizeof(frame));

    uint8_t command[2]{};
    REQUIRE_FALSE(readCmd(&sockets.client(), command));
    REQUIRE(sockets.client() == -1);
    REQUIRE(readPeerMessage(sockets.peer()) ==
            "second command signature error");
  }

  SECTION("encoded command is missing its second byte") {
    SocketPair sockets;
    const uint8_t first = 0xC4;
    writeBytes(sockets.peer(), &first, 1);

    uint8_t command[2]{};
    REQUIRE_FALSE(readCmd(&sockets.client(), command));
    REQUIRE(sockets.client() == -1);
    REQUIRE(readPeerMessage(sockets.peer()) == "second command missing");
  }
}
