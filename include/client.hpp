#pragma once

#include <cstdint>

bool handleNewClient(int serverFd, int clients[]);

bool startClientRuntime();
void stopClientRuntime();

void handleClient(const int* clientFd);
int pushClient(const int* clientFd, uint8_t byte);

void handleClientEnhanced(int* clientFd);
int pushClientEnhanced(const int* clientFd, uint8_t c, uint8_t d, bool log);
