#pragma once

void configureGpioInputPullup(int pin);

void disableTX();
void enableTX();

void restart();
void check_reset();
