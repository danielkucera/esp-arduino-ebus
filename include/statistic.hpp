#ifndef _STATISTIC_H_
#define _STATISTIC_H_

#include <WString.h>

// This file collects and provides statistical data based on the ebus classes Telegram and Sequence.
// The results are provided as a json string.

void resetStatistic();
void collectStatistic(const uint8_t byte);

String printCommandJsonStatistic();

#endif