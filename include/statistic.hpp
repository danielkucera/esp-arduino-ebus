#ifndef _STATISTIC_H_
#define _STATISTIC_H_

#include <WString.h>

void resetStatistic();
void collectStatistic(const uint8_t byte);

String printCommandJsonStatistic();

#endif