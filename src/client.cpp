#include "main.hpp"
#include "bus.hpp"

bool handleNewClient(WiFiServer &server, WiFiClient clients[]) {
  if (!server.hasClient())
    return false;

  // Find free/disconnected slot
  int i;
  for (i = 0; i < MAX_SRV_CLIENTS; i++) {
    if (!clients[i]) { // equivalent to !serverClients[i].connected()
      clients[i] = server.accept();
      clients[i].setNoDelay(true);
      break;
    }
  }

  // No free/disconnected slot so reject
  if (i == MAX_SRV_CLIENTS) {
    server.accept().println("busy");
    // hints: server.available() is a WiFiClient with short-term scope
    // when out of scope, a WiFiClient will
    // - flush() - all data will be sent
    // - stop() - automatically too
  }

  return true;
}

int pushClient(WiFiClient* client, uint8_t B){
    if (client->availableForWrite() >= AVAILABLE_THRESHOLD) {
        client->write(B);
        return 1;
    }
    return 0;
}

void handleClient(WiFiClient* client){
    while (client->available() && Bus.availableForWrite() > 0) {
      // working char by char is not very efficient
      Bus.write(client->read());
    }
}
