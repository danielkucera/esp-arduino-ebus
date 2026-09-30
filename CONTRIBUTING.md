# Contributing to esp-arduino-ebus

Thank you for your interest in contributing to the esp-arduino-ebus project! To maintain high code quality and architectural consistency, please follow these guidelines for all new code.

## Coding Standards

### C++ Version
*   **C++17**: Strictly adhere to C++17 features. Do not use C++20 or later features unless a polyfill is provided.

### Naming Conventions
*   **Classes and Structs**: `PascalCase` (e.g., `ConfigManager`, `WifiNetworkManager`).
*   **Methods and Functions**: `camelCase` (e.g., `getCommandsJson`, `handleValuesWrite`, `isValid`, `hasWriteCmd`). Exception: Container-like interface methods (e.g., `size()`, `empty()`, `clear()`) and std-style traits (e.g., `is_byte_range`, `has_to_json`) use `snake_case` for STL compatibility. Preserve the spelling required by external APIs when overriding or implementing them.
*   **Variables and Parameters**: `snake_case` (e.g., `wifi_ssid`, `poll_id`).
*   **Constants and `constexpr`**: `snake_case` (e.g., `baud_rate`, `max_data_bytes`). Prefer grouping related constants into classes as `static constexpr` members or specific namespaces.
*   **Private Members**: `snake_case_` with a trailing underscore (e.g., `task_handle_`, `stop_runner_`).
*   **Macros and Feature Flags**: `SCREAMING_SNAKE_CASE` (e.g., `COMMAND_CAPACITY`, `MAX_WIFI_CLIENTS`). This style is strictly reserved for preprocessor defines.
*   **Files and Directories**: `snake_case` (e.g., `config_manager.cpp`, `http_utils.hpp`).

### Memory Management
*   **Avoid Heap Allocation in Hot Paths**: While the core `ebus` library handles its own hot path with zero heap allocation, be mindful of allocations in performance-critical sections of the main application, especially within loops or frequent callbacks.
*   **Small Buffer Optimization**: The `ebus::Sequence` class (used by `Command` and `CommandManager`) utilizes an internal 64-byte stack buffer before falling back to the heap. Leverage this where appropriate.
*   **Standard Library**: Be mindful of `std::vector` and `std::string` usage in performance-critical sections to avoid hidden allocations.

### Component Categorization
The project integrates the `ebus` library, which has its own performance-critical sections. For the main application, components are categorized as follows:

**1. eBUS Library Hot Path (High Performance / Zero Allocation)**
These are internal components of the `ebus` library that process data byte-by-byte or perform time-critical protocol operations. Heap allocations are **forbidden** here.
*   `ebus::detail::Platform::Bus`, `ebus::detail::Core::Handler`, `ebus::detail::Core::Request`, `ebus::detail::Core::Telegram`, `ebus::Sequence`.

**2. Application Orchestration Path (Application Logic)**
These components manage high-level tasks like discovery, scheduling, network bridging, and UI interactions. Limited heap usage (e.g., `std::vector::reserve`, `std::map`) is permitted, but efficiency is still important for embedded targets.
*   `Mqtt`, `Cron`, `CommandManager`, `Http`, `ConfigManager`, `WifiNetworkManager`, `Adc`, `UpgradeManager`, `EspOtaManager`, `DNSServer`, `Logger`, `ClientManager` (from ebus lib), `AdapterVersion`.
*   **Note**: All orchestration components should enforce capacity limits to prevent memory exhaustion on embedded targets.

### Threading
*   **Thread Safety**: The public APIs of `ebus::Controller`, `Mqtt`, `Cron`, `CommandManager`, `Logger`, and `WifiNetworkManager` must be thread-safe.
*   **Prioritization**: Background tasks should use appropriate FreeRTOS priority levels to avoid starving critical protocol tasks (e.g., `ebus::detail::OrchestrationLimits::priority_low` for less critical tasks).
*   **Synchronization**: Internal state updates must be synchronized using mutexes or FreeRTOS primitives, as many components operate in separate tasks.

### Error Handling
*   **Metrics Over Exceptions**: Use the `ebus` library's internal `Metrics` system for protocol-level errors. Avoid using exceptions in the hot path. For application-level errors, use the `Logger` class.

### Protocol Compliance & Retries
*   **eBUS Protocol**: The `ebus` library handles eBUS protocol compliance, including NAK repetition and arbitration.
*   **Application Retries**: The `ebus` library's `Scheduler` provides configurable `max_send_attempts` and exponential backoff for high-level retries.
*   **Scan Filtering**: When implementing background tasks, only re-enqueue failed tasks if the failure was transient (e.g., arbitration loss). Whitelist successful `ebus::RequestResult` values rather than whitelisting `!success`.

### API Responsibility Model
To maintain stability and security on the ESP32-C3, the application follows a separation between the **Control Plane** (HTTP) and the **Data Plane** (MQTT).

| Feature | HTTP Support | MQTT Support | Rationale |
| :--- | :---: | :---: | :--- |
| **Telemetry (Values/Metrics)** | Read | **Publish** | Async updates are more efficient for monitoring. |
| **HA Auto-Discovery** | No | **Yes** | Standard for Home Assistant integration. |
| **Command Management** | **Yes** | No | Bulk RPC operations are resource-heavy and unsafe over MQTT. |
| **System (Wipe/Restart)** | **Yes** | No | Prevent remote bricking; requires local network access. |
| **Control (Value Write)** | **Yes** | **Yes** | HTTP for manual UI control; MQTT for automation. |

## Key Components

*   **ebus Library (`lib/ebus`)**: The core eBUS protocol stack, handling bus communication, arbitration, message processing, and scheduling.
*   **CommandManager (`include/app/command_manager.hpp`, `src/app/command_manager.cpp`)**: Manages eBUS command configurations and their associated data, including persistence to LittleFS.
*   **Mqtt (`include/app/mqtt.hpp`, `src/app/mqtt.cpp`)**: Handles MQTT communication for publishing values and receiving commands.
*   **MqttHA (`include/app/mqtt_ha.hpp`, `src/app/mqtt_ha.cpp`)**: Handles Home Assistant MQTT auto-discovery.
*   **Cron (`include/app/cron.hpp`, `src/app/cron.cpp`)**: Manages scheduled eBUS write operations based on cron-like expressions.
*   **Http (`include/network/http.hpp`, `src/network/http.cpp`)**: Sets up the web server and HTTP routes.
*   **ConfigManager (`include/config/config_manager.hpp`, `src/config/config_manager.cpp`)**: Manages persistent configuration settings using NVS.
*   **WifiNetworkManager (`include/network/wifi_network_manager.hpp`, `src/network/wifi_network_manager.cpp`)**: Handles Wi-Fi connectivity (STA and AP modes), mDNS, and static IP configuration.
*   **Adc (`include/hardware/adc.hpp`, `src/hardware/adc.cpp`)**: Manages ADC sampling and streaming for diagnostic purposes.
*   **UpgradeManager (`include/system/upgrade_manager.hpp`, `src/system/upgrade_manager.cpp`)**: Handles firmware updates via HTTP upload or URL.
*   **EspOtaManager (`include/system/esp_ota_manager.hpp`, `src/system/esp_ota_manager.cpp`)**: Handles firmware updates via ESP-OTA protocol (UDP).
*   **DNSServer (`include/network/dns_server.hpp`, `src/network/dns_server.cpp`)**: Provides DNS services for the captive portal in AP mode.
*   **Logger (`include/system/logger.hpp`, `src/system/logger.cpp`)**: Manages application logging to a circular buffer and serial output.
*   **SystemMonitor (`include/system/system_monitor.hpp`, `src/system/system_monitor.cpp`)**: Monitors system health metrics (heap, stack, task stats, Wi-Fi RSSI) and publishes via MQTT.
*   **AdapterVersion (`include/system/adapter_version.hpp`, `src/system/adapter_version.cpp`)**: Provides adapter hardware and software version information from eFuse.

## Project Structure

*   `include/`: Public headers for the main application.
*   `src/`: Source files for the main application components.
*   `static/`: Static web assets (HTML, CSS, JS) for the web UI.

## How to Submit Changes

1.  **Small Commits**: Keep your commits atomic and focused on a single change.
2.  **Naming**: Ensure all new files follow the `snake_case` naming rule.
3.  **Headers**: Ensure every new file starts with the standard project license header.
4.  **Pull Requests**: Submit your changes via a Pull Request. Ensure that all tests pass before submission.

## Testing

### Host Tests (Catch2 + CMake)

Host tests run on the development machine (no hardware required). The suite builds nine test executables (`api`, `app`, `bridge`, `config`, `http_utils`, `identity`, `logger`, `network`, `system`) with per-case CTest entries (via `catch_discover_tests`); test files mirror `src/` (`test_host/{api,app,bridge,config,http_utils,identity,logger,network,system}/`).

```bash
cmake -S test_host -B test_host/build
cmake --build test_host/build
ctest --test-dir test_host/build -j$(nproc) --timeout 10 --output-on-failure
```

`test_host` resolves the ebus library itself (local `lib/ebus` symlink or pinned FetchContent — `EBUS_PIN` must match the `lib_deps` pin); sanitizer runs pass `-DDISABLE_CATCH_DISCOVERY=ON`.

### ebus Library Source Linking

The `ebus` library is vendored into `lib/ebus/` and is built as part of the PlatformIO and host test build. For development, you may want to modify the library source directly or link it from an external checkout.

**Option A: Work directly in the vendored copy**

Edit files under `lib/ebus/` directly. The library has its own git repository and test suite:

```bash
cd lib/ebus && mkdir build && cd build && cmake -DEBUS_SIMULATION=ON .. && make && ctest
```

**Option B: Link from an external checkout**

If you have a separate clone of the `ebus` library, replace the vendored copy with a symlink:

```bash
rm -rf lib/ebus
ln -s /path/to/ebus/lib/ebus lib/ebus
```

**Important**: The `ebus` library's compile-time macros (defined in `lib/ebus/CMakeLists.txt` and `lib/ebus/include/ebus/detail/protocol_limits.hpp`) must be mirrored in `platformio.ini` `build_flags` for the `esp32-c3-internal` environment. Without this, the library silently falls back to its built-in defaults, causing mismatches.

### ebus Library Tests

```bash
cd lib/ebus && mkdir build && cd build && cmake -DEBUS_SIMULATION=ON .. && make && ctest
```
