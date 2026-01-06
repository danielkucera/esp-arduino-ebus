#pragma once

#include <Arduino.h>

enum class LogLevel { INFO, WARN, ERROR };

void addLog(LogLevel level, String message);
String getLogs();
