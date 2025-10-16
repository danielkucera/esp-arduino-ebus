#include "client.hpp"

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

#if defined(EBUS_INTERNAL)
#include <algorithm>

AbstractClient::AbstractClient(WiFiClient* client, ebus::Request* request,
                               bool write)
    : client(client), request(request), write(write) {}

bool AbstractClient::isWriteCapable() const { return write; }

bool AbstractClient::isConnected() const {
  return client && client->connected();
}

void AbstractClient::stop() {
  if (client) client->stop();
}

ReadOnlyClient::ReadOnlyClient(WiFiClient* client, ebus::Request* request)
    : AbstractClient(client, request, false) {}

bool ReadOnlyClient::available() const { return false; }

bool ReadOnlyClient::readByte(uint8_t& byte) { return false; }

bool ReadOnlyClient::writeBytes(const std::vector<uint8_t>& bytes) {
  if (!isConnected() || bytes.empty()) return false;

  client->write(bytes.data(), bytes.size());
  return true;
}

bool ReadOnlyClient::handleBusData(const uint8_t& byte) { return false; }

RegularClient::RegularClient(WiFiClient* client, ebus::Request* request)
    : AbstractClient(client, request, true) {}

bool RegularClient::available() const { return client && client->available(); }

bool RegularClient::readByte(uint8_t& byte) {
  if (available()) {
    byte = client->read();
    return true;
  }
  return false;
}

bool RegularClient::writeBytes(const std::vector<uint8_t>& bytes) {
  if (!isConnected() || bytes.empty()) return false;

  client->write(bytes.data(), bytes.size());
  return true;
}

bool RegularClient::handleBusData(const uint8_t& byte) {
  // Handle bus response according to last command
  switch (request->getResult()) {
    case ebus::RequestResult::observeSyn:
    case ebus::RequestResult::firstLost:
    case ebus::RequestResult::firstError:
    case ebus::RequestResult::retryError:
    case ebus::RequestResult::secondLost:
    case ebus::RequestResult::secondError:
      return false;
    case ebus::RequestResult::observeData:
    case ebus::RequestResult::firstSyn:
    case ebus::RequestResult::firstRetry:
    case ebus::RequestResult::retrySyn:
    case ebus::RequestResult::firstWon:
    case ebus::RequestResult::secondWon:
      writeBytes({byte});
      return true;
    default:
      break;
  }
  return false;
}

EnhancedClient::EnhancedClient(WiFiClient* client, ebus::Request* request)
    : AbstractClient(client, request, true) {}

bool EnhancedClient::available() const { return client && client->available(); }

bool EnhancedClient::readByte(uint8_t& byte) {
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
  if ((b1 & 0xc0) != 0xc0 || (b2 & 0xc0) != 0x80) {
    // Invalid signature, protocol error
    writeBytes({ERROR_HOST, ERR_FRAMING});
    client->stop();
    return false;
  }

  // Decode command and data according to enhanced protocol
  uint8_t cmd = (b1 >> 2) & 0x0f;
  uint8_t data = ((b1 & 0x03) << 6) | (b2 & 0x3f);

  // Handle commands
  switch (cmd) {
    case CMD_INIT:
      writeBytes({RESETTED, 0x0});
      return false;
    case CMD_SEND:
      byte = data;
      return true;
    case CMD_START:
      if (data == SYN) return false;
      byte = data;
      return true;
    case CMD_INFO:
      return false;
    default:
      break;
  }

  return false;
}

bool EnhancedClient::writeBytes(const std::vector<uint8_t>& bytes) {
  if (!isConnected() || bytes.empty()) return false;

  uint8_t cmd = RECEIVED;
  uint8_t data = bytes[0];

  if (bytes.size() == 2) {
    cmd = bytes[0];
    data = bytes[1];
  }

  // Short form for data < 0x80
  if (bytes.size() == 1 && data < 0x80) {
    client->write(data);
  } else {
    uint8_t out[2];
    out[0] = 0xc0 | (cmd << 2) | (data >> 6);
    out[1] = 0x80 | (data & 0x3f);
    client->write(out, 2);
  }
  return true;
}

bool EnhancedClient::handleBusData(const uint8_t& byte) {
  // Handle bus response according to last command
  switch (request->getResult()) {
    case ebus::RequestResult::observeSyn:
    case ebus::RequestResult::firstLost:
    case ebus::RequestResult::secondLost:
      writeBytes({FAILED, byte});
      return false;
    case ebus::RequestResult::firstError:
    case ebus::RequestResult::retryError:
    case ebus::RequestResult::secondError:
      writeBytes({ERROR_EBUS, ERR_FRAMING});
      return false;
    case ebus::RequestResult::observeData:
      writeBytes({RECEIVED, byte});
      return true;
    case ebus::RequestResult::firstSyn:
    case ebus::RequestResult::firstRetry:
    case ebus::RequestResult::retrySyn:
      // Waiting for arbitration, do nothing
      return true;
    case ebus::RequestResult::firstWon:
    case ebus::RequestResult::secondWon:
      writeBytes({STARTED, byte});
      return true;
    default:
      break;
  }
  return false;
}

ClientManager clientManager;

ClientManager::ClientManager()
    : readonlyServer(3334), regularServer(3333), enhancedServer(3335) {}

void ClientManager::start(ebus::Bus* bus, ebus::Request* request,
                          ebus::ServiceRunnerFreeRtos* serviceRunner) {
  readonlyServer.begin();
  regularServer.begin();
  enhancedServer.begin();

  this->request = request;
  this->serviceRunner = serviceRunner;

  clientByteQueue = new ebus::Queue<uint8_t>();

  request->setExternalBusRequestedCallback([this]() { busRequested = true; });

  serviceRunner->addByteListener(
      [this](const uint8_t& byte) { clientByteQueue->try_push(byte); });

  // Start the clientManagerRunner task
  xTaskCreate(&ClientManager::taskFunc, "clientManagerRunner", 4096, this, 3,
              &clientManagerTaskHandle);
}

void ClientManager::stop() { stopRunner = true; }

void ClientManager::taskFunc(void* arg) {
  ClientManager* self = static_cast<ClientManager*>(arg);
  AbstractClient* activeClient = nullptr;
  BusState busState = BusState::Idle;
  uint8_t receiveByte = 0;

  for (;;) {
    if (self->stopRunner) vTaskDelete(NULL);

    // Check for new clients
    self->acceptClients();

    // Clean up disconnected active client
    if (activeClient && !activeClient->isConnected()) {
      activeClient->stop();
      activeClient = nullptr;
      busState = BusState::Idle;
      self->busRequested = false;
      ebus::request->reset();
    }

    // Select new active client if idle
    if (!activeClient && busState == BusState::Idle) {
      for (size_t i = 0; i < self->clients.size(); ++i) {
        AbstractClient* client = self->clients[i].get();
        if (client->isConnected() && client->isWriteCapable() &&
            client->available()) {
          activeClient = client;
          busState = BusState::Request;
          self->busRequested = false;
          break;
        }
      }
    }

    // Request bus access
    if (activeClient && busState == BusState::Request) {
      if (ebus::request->busAvailable()) {
        uint8_t firstByte = 0;
        if (activeClient->readByte(firstByte)) {
          ebus::request->requestBus(firstByte, true);
          busState = BusState::Response;
        } else {
          // Client initialized or error
          activeClient = nullptr;
          busState = BusState::Idle;
          self->busRequested = false;
          ebus::request->reset();
        }
      }
    }

    // Transmit to bus if needed
    if (activeClient && busState == BusState::Transmit) {
      uint8_t sendByte = 0;
      if (activeClient->readByte(sendByte)) {
        ebus::bus->writeByte(sendByte);
        busState = BusState::Response;
      }
    }

    // Process received bytes from bus
    while (self->clientByteQueue->try_pop(receiveByte)) {
      updateLastComms();

      if (activeClient) {
        if ((busState == BusState::Response ||
             busState == BusState::Transmit) &&
            self->busRequested) {
          if (activeClient->handleBusData(receiveByte)) {
            // Continue transmitting if needed
            busState = BusState::Transmit;
          } else {
            // Transaction done or error
            activeClient = nullptr;
            busState = BusState::Idle;
            self->busRequested = false;
            ebus::request->reset();
          }
        }
      }

      // Forward to all other clients
      for (size_t i = 0; i < self->clients.size(); ++i) {
        AbstractClient* client = self->clients[i].get();
        if (client != activeClient && client->isConnected()) {
          client->writeBytes({receiveByte});
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void ClientManager::acceptClients() {
  // Accept read-only clients
  while (readonlyServer.hasClient()) {
    WiFiClient* client = new WiFiClient(readonlyServer.accept());
    client->setNoDelay(true);
    clients.push_back(make_unique<ReadOnlyClient>(client, request));
  }

  // Accept regular clients
  while (regularServer.hasClient()) {
    WiFiClient* client = new WiFiClient(regularServer.accept());
    client->setNoDelay(true);
    clients.push_back(make_unique<RegularClient>(client, request));
  }

  // Accept enhanced clients
  while (enhancedServer.hasClient()) {
    WiFiClient* client = new WiFiClient(enhancedServer.accept());
    client->setNoDelay(true);
    clients.push_back(make_unique<EnhancedClient>(client, request));
  }

  // Clean up disconnected clients
  clients.erase(
      std::remove_if(clients.begin(), clients.end(),
                     [](const std::unique_ptr<AbstractClient>& client) {
                       if (!client->isConnected()) {
                         client->stop();  // <-- ensure socket is closed
                         return true;
                       }
                       return false;
                     }),
      clients.end());
}

#endif