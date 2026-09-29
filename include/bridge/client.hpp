#pragma once

#if !defined(EBUS_INTERNAL)

#include <cstdint>

bool handleNewClient(int server_fd, int clients[]);

bool startClientRuntime();
void stopClientRuntime();

void handleClient(const int* client_fd);
int pushClient(const int* client_fd, uint8_t byte);

void handleClientEnhanced(int* client_fd);
int pushClientEnhanced(const int* client_fd, uint8_t c, uint8_t d, bool log);

#endif
