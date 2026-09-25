#include <cassert>
#include <cstdint>
#include <iostream>

#include "bridge_socket_io.hpp"

struct Pair {
  int fd[2];
  bridge::EnhancedCommandReader reader;
  Pair() { assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fd) == 0); }
  ~Pair() { close(fd[0]); close(fd[1]); }
  void sendByte(uint8_t byte) { assert(send(fd[0], &byte, 1, 0) == 1); }
};

int main() {
  using bridge::CommandRead;
  uint8_t command[2] = {};
  assert(!bridge::socketHasInput(-1));
  {
    Pair p;
    assert(!bridge::socketHasInput(p.fd[1]));
    assert(p.reader.read(p.fd[1], command) == CommandRead::incomplete);
    // Every command/data combination survives an arbitrary TCP split.
    for (unsigned cmd = 0; cmd < 16; ++cmd) {
      for (unsigned data = 0; data < 256; ++data) {
        p.sendByte(0xc0 | (cmd << 2) | (data >> 6));
        assert(bridge::socketHasInput(p.fd[1]));
        for (int i = 0; i < 3; ++i)
          assert(p.reader.read(p.fd[1], command) == CommandRead::incomplete);
        p.sendByte(0x80 | (data & 0x3f));
        assert(p.reader.read(p.fd[1], command) == CommandRead::ready);
        assert(command[0] == cmd && command[1] == data);
        assert(!bridge::socketHasInput(p.fd[1]));
      }
    }
    for (unsigned byte = 0; byte < 128; ++byte) {
      p.sendByte(byte);
      assert(p.reader.read(p.fd[1], command) == CommandRead::ready);
      assert(command[0] == 1 && command[1] == byte);
    }
    // Coalesced INIT, compact SEND, START, then a split INFO prefix.
    const uint8_t batch[] = {0xc0, 0x80, 0x31, 0xc8, 0xb1, 0xcc};
    assert(send(p.fd[0], batch, sizeof(batch), 0) == sizeof(batch));
    assert(p.reader.read(p.fd[1], command) == CommandRead::ready);
    assert(command[0] == 0 && command[1] == 0);
    assert(p.reader.read(p.fd[1], command) == CommandRead::ready);
    assert(command[0] == 1 && command[1] == 0x31);
    assert(p.reader.read(p.fd[1], command) == CommandRead::ready);
    assert(command[0] == 2 && command[1] == 0x31);
    assert(p.reader.read(p.fd[1], command) == CommandRead::incomplete);
    p.sendByte(0x80);
    assert(p.reader.read(p.fd[1], command) == CommandRead::ready);
    assert(command[0] == 3 && command[1] == 0);
    shutdown(p.fd[0], SHUT_WR);
    assert(!bridge::socketHasInput(p.fd[1]));
    assert(p.reader.read(p.fd[1], command) == CommandRead::closed);
  }
  for (unsigned invalid : {0x80, 0xbf}) {
    Pair p;
    p.sendByte(invalid);
    assert(p.reader.read(p.fd[1], command) == CommandRead::invalid);
  }
  for (unsigned invalid : {0x00, 0x7f, 0xc0, 0xff}) {
    Pair p;
    p.sendByte(0xc8);
    p.sendByte(invalid);
    assert(p.reader.read(p.fd[1], command) == CommandRead::invalid);
  }
  {
    Pair p;
    p.sendByte(0xc8);
    shutdown(p.fd[0], SHUT_WR);
    assert(p.reader.read(p.fd[1], command) == CommandRead::closed);
    // No partial command is ever executed, even on truncated EOF.
  }
  {
    Pair a, b;
    a.sendByte(0xc8);
    assert(a.reader.read(a.fd[1], command) == CommandRead::incomplete);
    b.sendByte(0xc0);
    b.sendByte(0x80);
    assert(b.reader.read(b.fd[1], command) == CommandRead::ready);
    assert(command[0] == 0 && command[1] == 0);
    a.sendByte(0xb1);
    assert(a.reader.read(a.fd[1], command) == CommandRead::ready);
    assert(command[0] == 2 && command[1] == 0x31);
    a.sendByte(0xc8);
    assert(a.reader.read(a.fd[1], command) == CommandRead::incomplete);
    a.reader.reset();  // a new connection must never inherit an old prefix
    a.sendByte(0x31);
    assert(a.reader.read(a.fd[1], command) == CommandRead::ready);
    assert(command[0] == 1 && command[1] == 0x31);
  }
  std::cout << "Bridge socket readiness and stream framing tests passed\n";
}
