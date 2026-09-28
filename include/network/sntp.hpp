#pragma once

#if defined(EBUS_INTERNAL)

#include <ebus/detail/json_writer.hpp>

#include "config/app_config.hpp"

void initSNTP(const AppConfig::Sntp& sntp);
void setTimezone(const AppConfig::Sntp& sntp);

// Renders the "sntp" status section. Colocated here (rather than in the
// status model) because this module already owns the Sntp config slice.
// Writes fields only: the caller opens the named object scope.
void appendSntpStatus(ebus::detail::JsonWriter& writer,
                      const AppConfig::Sntp& sntp);

#endif
