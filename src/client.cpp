#include "client.hpp"

#include <WiFiClient.h>

#include "bus.hpp"
#include "main.hpp"

#define M1 0b11000000
#define M2 0b10000000

enum requests { CMD_INIT = 0, CMD_SEND, CMD_START, CMD_INFO };

bool handleNewClient(WiFiServer* server, WiFiClient clients[]) {
  if (!server->hasClient()) return false;

  // Find free/disconnected slot
  int i;
  for (i = 0; i < MAX_WIFI_CLIENTS; i++) {
    if (!clients[i]) {  // equivalent to !serverClients[i].connected()
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

bool getClientData(WiFiClient* client, uint8_t& byte) {
  if (client->available()) {
    byte = client->read();
    return true;
  }
  return false;
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

bool getClientDataEnhanced(WiFiClient* client, uint8_t& byte) {
  while (client->available()) {
    uint8_t data[2];
    if (read_cmd(client, data)) {
      uint8_t c = data[0];
      uint8_t d = data[1];
      if (c == CMD_INIT) {
        send_res(client, RESETTED, 0x0);
        return true;
      }
      if (c == CMD_START) {
        if (d == SYN) {
          return false;
        } else {
          // start request
          byte = d;
          return true;
        }
      }
      if (c == CMD_SEND) {
        byte = d;
        return true;
      }
      if (c == CMD_INFO) {
        return false;
      }
    }
  }
  return false;
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

#if defined(EBUS_INTERNAL)
#include <algorithm>

volatile bool stopClientManagerRunner = false;

AbstractClient::AbstractClient(WiFiClient* client, bool write)
    : client(client), write(write) {}

bool AbstractClient::isWriteCapable() const { return write; }

bool AbstractClient::isConnected() const {
  return client && client->connected();
}

void AbstractClient::stop() {
  if (client) client->stop();
}

RegularClient::RegularClient(WiFiClient* client)
    : AbstractClient(client, true) {}

bool RegularClient::available() const { return client && client->available(); }

bool RegularClient::readByte(uint8_t& byte) {
  if (available()) {
    byte = client->read();
    return true;
  }
  return false;
}

bool RegularClient::writeBytes(const std::vector<uint8_t>& bytes) {
  if (isConnected()) {
    for (size_t i = 0; i < bytes.size(); ++i) client->write(bytes[i]);
    return true;
  }
  return false;
}

ReadOnlyClient::ReadOnlyClient(WiFiClient* client)
    : AbstractClient(client, false) {}

bool ReadOnlyClient::available() const { return false; }

bool ReadOnlyClient::readByte(uint8_t&) { return false; }

bool ReadOnlyClient::writeBytes(const std::vector<uint8_t>& bytes) {
  if (isConnected()) {
    client->write(&bytes[0], bytes.size());
    return true;
  }
  return false;
}

EnhancedClient::EnhancedClient(WiFiClient* client)
    : AbstractClient(client, true) {}

bool EnhancedClient::available() const { return client && client->available(); }

bool EnhancedClient::readByte(uint8_t& byte) {
  if (!available()) return false;
  int b1 = client->peek();
  if (b1 < 0) return false;

  if (b1 < 0x80) {
    // Short form: just a data byte, no prefix
    byte = client->read();
    return true;
  }

  // Full enhanced protocol: need two bytes
  if (client->available() < 2) return false;
  b1 = client->read();
  int b2 = client->read();

  // Check signatures
  if ((b1 & 0xC0) != 0xC0 || (b2 & 0xC0) != 0x80) {
    // Invalid signature, protocol error
    return false;
  }

  // Decode command and data according to enhanced protocol
  uint8_t cmd = (b1 >> 2) & 0x0F;
  uint8_t data = ((b1 & 0x03) << 6) | (b2 & 0x3F);

  // For <SEND> requests, just return the data byte
  if (cmd == 1) {  // CMD_SEND
    byte = data;
    return true;
  }
  // You can handle other commands (INIT, START, INFO, etc.) here if needed

  return false;
}

bool EnhancedClient::writeBytes(const std::vector<uint8_t>& bytes) {
  if (!isConnected()) return false;
  for (size_t i = 0; i < bytes.size(); ++i) {
    uint8_t data = bytes[i];
    if (data < 0x80) {
      // Short form: just the data byte
      client->write(data);
    } else {
      // Full form: <RECEIVED> prefix
      uint8_t cmd =
          1;  // <RECEIVED> is CMD_RECEIVED (use correct value if different)
      uint8_t out[2];
      out[0] = 0xC0 | (cmd << 2) | (data >> 6);
      out[1] = 0x80 | (data & 0x3F);
      client->write(out, 2);
    }
  }
  return true;
}

ClientManager clientManager;

ClientManager::ClientManager()
    : regularServer(3333), readonlyServer(3334), enhancedServer(3335) {}

void ClientManager::start(ebus::Queue<uint8_t>* clientByteQueue) {
  regularServer.begin();
  readonlyServer.begin();
  enhancedServer.begin();

  this->clientByteQueue = clientByteQueue;

  // Start the clientManagerRunner task
  xTaskCreate(&ClientManager::taskFunc, "clientManagerRunner", 4096, this, 3,
              &clientManagerTaskHandle);
}

void ClientManager::stop() { stopClientManagerRunner = true; }

void ClientManager::taskFunc(void* arg) {
  ClientManager* self = static_cast<ClientManager*>(arg);
  for (;;) {
    if (stopClientManagerRunner) vTaskDelete(NULL);

    self->acceptClients();
    self->processClients();
    self->processBusData();

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void ClientManager::acceptClients() {
  // Accept regular clients
  while (regularServer.hasClient()) {
    WiFiClient* c = new WiFiClient(regularServer.accept());
    c->setNoDelay(true);
    clients.push_back(make_unique<RegularClient>(c));
  }
  // Accept read-only clients
  while (readonlyServer.hasClient()) {
    WiFiClient* c = new WiFiClient(readonlyServer.accept());
    c->setNoDelay(true);
    clients.push_back(make_unique<ReadOnlyClient>(c));
  }
  // Accept enhanced clients
  while (enhancedServer.hasClient()) {
    WiFiClient* c = new WiFiClient(enhancedServer.accept());
    c->setNoDelay(true);
    clients.push_back(make_unique<EnhancedClient>(c));
  }

  // Clean up disconnected clients
  clients.erase(
      std::remove_if(clients.begin(), clients.end(),
                     [](const std::unique_ptr<AbstractClient>& client) {
                       return !client->isConnected();
                     }),
      clients.end());
}

void ClientManager::processClients() {
  for (size_t i = 0; i < clients.size(); ++i) {
    const AbstractClient* client = clients[i].get();
    if (!client->isConnected() || !client->isWriteCapable()) continue;

    // std::vector<uint8_t> masterBytes;
    // uint8_t byte;
    // // Collect bytes until we have a valid master telegram
    // while (client->readByte(byte)) {
    //   masterBytes.push_back(byte);
    //   if (masterBytes.size() >= 5) {  // Minimum telegram size
    //     // Try to parse telegram
    //     ebus::Telegram telegram;
    //     telegram.createMaster(masterBytes[0], masterBytes);  // QQ = src
    //     if (telegram.getMasterState() == ebus::SequenceState::seq_ok) {
    //       // Forward CRC and ACK to client
    //       std::vector<uint8_t> crcAck;
    //       crcAck.push_back(telegram.getMasterCRC());
    //       crcAck.push_back(telegram.getMasterACK());
    //       client->writeBytes(crcAck);

    //       // Request bus and send master part
    //       handler->enqueueActiveMessage(masterBytes);
    //       // Handler will handle bus arbitration and sending

    //       // Wait for slave part (simulate: poll handler for slave
    //       telegram) while (handler->getState() !=
    //       ebus::HandlerState::releaseBus) {
    //         // Optionally: add timeout
    //       }
    //       auto slaveSeq = handler->activeTelegram.getSlave();
    //       std::vector<uint8_t> slaveBytes = slaveSeq.to_vector();
    //       // Forward slave part, CRC, and ACK to client
    //       if (!slaveBytes.empty()) {
    //         client->writeBytes(slaveBytes);
    //         std::vector<uint8_t> slaveCrcAck;
    //         slaveCrcAck.push_back(handler->activeTelegram.getSlaveCRC());
    //         slaveCrcAck.push_back(handler->activeTelegram.getSlaveACK());
    //         client->writeBytes(slaveCrcAck);
    //       }
    //       break;  // Done with this telegram
    //     }
    //   }
    // }
  }
}

void ClientManager::processBusData() {
  uint8_t busByte;
  while (clientByteQueue->try_pop(busByte)) {
    std::vector<uint8_t> busData{busByte};
    // Forward to all clients (including read-only)
    for (size_t i = 0; i < clients.size(); ++i) {
      AbstractClient* client = clients[i].get();
      if (client->isConnected()) {
        client->writeBytes(busData);
      }
    }
  }
  updateLastComms();
}

#endif