#pragma once

#include <cstdint>

bool handleNewClient(int serverFd, int clients[]);

bool startClientAcceptTask(int serverFd, int clients[], int enhancedServerFd,
						   int enhancedClients[], int readOnlyServerFd,
						   int readOnlyClients[]);
void stopClientAcceptTask();

void handleClient(int* clientFd);
int pushClient(int* clientFd, uint8_t byte);

void handleClientEnhanced(int* clientFd);
int pushClientEnhanced(int* clientFd, uint8_t c, uint8_t d, bool log);

