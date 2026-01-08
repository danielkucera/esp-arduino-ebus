#pragma once

#include <Arduino.h>

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

void addLog(LogLevel level, String message);
String getLogs();
