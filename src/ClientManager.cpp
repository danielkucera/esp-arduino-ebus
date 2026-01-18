#if defined(EBUS_INTERNAL)
#include "ClientManager.hpp"

#include <algorithm>

// C++11 compatible make_unique
template <class T, class... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

ClientManager clientManager;

ClientManager::ClientManager()
    : readonlyServer(3334), regularServer(3333), enhancedServer(3335) {}

void ClientManager::setLastCommsCallback(LastCommsCallback callback) {
  lastCommsCallback = std::move(callback);
}

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
      self->lastCommsCallback();

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