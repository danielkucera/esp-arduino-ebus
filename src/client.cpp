#include "client.hpp"

#include "BusType.hpp"
#include "main.hpp"

#define M1 0b11000000
#define M2 0b10000000

enum requests { CMD_INIT = 0, CMD_SEND, CMD_START, CMD_INFO };

bool handleNewClient(WiFiServer* server, WiFiClient clients[]) {
  if (!server->hasClient()) return false;

  // Find free/disconnected slot
  int i;
  for (i = 0; i < MAX_WIFI_CLIENTS; i++) {
    if (!clients[i]) {  // equivalent to !clients[i].connected()
      clients[i] = server->accept();
      clients[i].setNoDelay(true);
      break;
    }
  }

  // No free/disconnected slot so reject
  if (i == MAX_WIFI_CLIENTS) {
    server->accept().println("busy");
    // hints: server.available() is a WiFiClient with short-term scope
    // when out of scope, a WiFiClient will
    // - flush() - all data will be sent
    // - stop() - automatically too
  }

  return true;
}

void handleClient(WiFiClient* client) {
  while (client->available() && Bus.availableForWrite() > 0) {
    // working char by char is not very efficient
    Bus.write(client->read());
  }
}

int pushClient(WiFiClient* client, uint8_t byte) {
  if (client->availableForWrite() >= AVAILABLE_THRESHOLD) {
    client->write(byte);
    return 1;
  }
  return 0;
}

void decode(int b1, int b2, uint8_t (&data)[2]) {
  data[0] = (b1 >> 2) & 0b1111;
  data[1] = ((b1 & 0b11) << 6) | (b2 & 0b00111111);
}

void encode(uint8_t c, uint8_t d, uint8_t (&data)[2]) {
  data[0] = M1 | c << 2 | d >> 6;
  data[1] = M2 | (d & 0b00111111);
}

void send_res(WiFiClient* client, uint8_t c, uint8_t d) {
  uint8_t data[2];
  encode(c, d, data);
  client->write(data, 2);
}

void process_cmd(WiFiClient* client, uint8_t c, uint8_t d) {
  if (c == CMD_INIT) {
    send_res(client, RESETTED, 0x0);
    return;
  }
  if (c == CMD_START) {
    if (d == SYN) {
      clearArbitrationClient();
      DEBUG_LOG("CMD_START SYN\n");
      return;
    } else {
      // start arbitration
      WiFiClient* cl = client;
      uint8_t ad = d;
      if (!setArbitrationClient(client, d)) {
        if (cl != client) {
          // only one client can be in arbitration
          DEBUG_LOG("CMD_START ONGOING 0x%02 0x%02x\n", ad, d);
          send_res(client, ERROR_HOST, ERR_FRAMING);
          return;
        } else {
          DEBUG_LOG("CMD_START REPEAT 0x%02x\n", d);
        }
      } else {
        DEBUG_LOG("CMD_START 0x%02x\n", d);
      }
      setArbitrationClient(client, d);
      return;
    }
  }
  if (c == CMD_SEND) {
    DEBUG_LOG("SEND 0x%02x\n", d);
    Bus.write(d);
    return;
  }
  if (c == CMD_INFO) {
    // if needed, set bit 0 as reply to INIT command
    return;
  }
}

bool read_cmd(WiFiClient* client, uint8_t (&data)[2]) {
  int b, b2;

  b = client->read();

  if (b < 0) {
    // available and read -1 ???
    return false;
  }

  if (b < 0b10000000) {
    data[0] = CMD_SEND;
    data[1] = b;
    return true;
  }

  if (b < 0b11000000) {
    DEBUG_LOG("first command signature error\n");
    client->write("first command signature error");
    // first command signature error
    client->stop();
    return false;
  }

  b2 = client->read();

  if (b2 < 0) {
    // second command missing
    DEBUG_LOG("second command missing\n");
    client->write("second command missing");
    client->stop();
    return false;
  }

  if ((b2 & 0b11000000) != 0b10000000) {
    // second command signature error
    DEBUG_LOG("second command signature error\n");
    client->write("second command signature error");
    client->stop();
    return false;
  }

  decode(b, b2, data);
  return true;
}

void handleClientEnhanced(WiFiClient* client) {
  while (client->available()) {
    uint8_t data[2];
    if (read_cmd(client, data)) {
      process_cmd(client, data[0], data[1]);
    }
  }
}

int pushClientEnhanced(WiFiClient* client, uint8_t c, uint8_t d, bool log) {
  if (log) {
    DEBUG_LOG("DATA           0x%02x 0x%02x\n", c, d);
  }
  if (client->availableForWrite() >= AVAILABLE_THRESHOLD) {
    send_res(client, c, d);
    return 1;
  }
  return 0;
}
