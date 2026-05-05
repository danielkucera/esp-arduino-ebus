# Contributing to esp-arduino-ebus

Thank you for your interest in contributing to the esp-arduino-ebus project! To maintain high code quality and architectural consistency, please follow these guidelines for all new code.

## Coding Standards

### C++ Version
*   **C++17**: Strictly adhere to C++17 features. Do not use C++20 or later features unless a polyfill is provided.

### Naming Conventions
*   **Classes and Structs**: `PascalCase` (e.g., `ConfigManager`, `WifiNetworkManager`).
*   **Methods and Functions**: `camelCase` (e.g., `getCommandsJson`, `handleValuesWrite`).
*   **Variables and Parameters**: `snake_case` (e.g., `wifi_ssid`, `poll_id`).
*   **Private Members**: `snake_case_` with a trailing underscore (e.g., `task_handle_`, `stop_runner_`).
*   **Files and Directories**: `snake_case` (e.g., `config_manager.cpp`, `http_utils.hpp`).

### Memory Management
*   **Avoid Heap Allocation in Hot Paths**: While the core `ebus` library handles its own hot path with zero heap allocation, be mindful of allocations in performance-critical sections of the main application, especially within loops or frequent callbacks.
*   **Small Buffer Optimization**: The `ebus::Sequence` class (used by `Command` and `Store`) utilizes an internal 64-byte stack buffer before falling back to the heap. Leverage this where appropriate.
*   **Standard Library**: Be mindful of `std::vector` and `std::string` usage in performance-critical sections to avoid hidden allocations.

### Component Categorization
The project integrates the `ebus` library, which has its own performance-critical sections. For the main application, components are categorized as follows:

**1. eBUS Library Hot Path (High Performance / Zero Allocation)**
These are internal components of the `ebus` library that process data byte-by-byte or perform time-critical protocol operations. Heap allocations are **forbidden** here.
*   `ebus::detail::Platform::Bus`, `ebus::detail::Core::Handler`, `ebus::detail::Core::Request`, `ebus::detail::Core::Telegram`, `ebus::Sequence`.

**2. Application Orchestration Path (Application Logic)**
These components manage high-level tasks like discovery, scheduling, network bridging, and UI interactions. Limited heap usage (e.g., `std::vector::reserve`, `std::map`) is permitted, but efficiency is still important for embedded targets.
*   `Mqtt`, `Cron`, `Store`, `Http`, `ConfigManager`, `WifiNetworkManager`, `Adc`, `UpgradeManager`, `EspOtaManager`, `DNSServer`, `Logger`, `ClientManager` (from ebus lib), `AdapterVersion`.
*   **Note**: All orchestration components should enforce capacity limits to prevent memory exhaustion on embedded targets.

### Threading
*   **Thread Safety**: The public APIs of `ebus::Controller`, `Mqtt`, `Cron`, `Store`, `Logger`, and `WifiNetworkManager` must be thread-safe.
*   **Prioritization**: Background tasks should use appropriate FreeRTOS priority levels to avoid starving critical protocol tasks (e.g., `ebus::detail::OrchestrationLimits::priority_low` for less critical tasks).
*   **Synchronization**: Internal state updates must be synchronized using mutexes or FreeRTOS primitives, as many components operate in separate tasks.

### Error Handling
*   **Metrics Over Exceptions**: Use the `ebus` library's internal `Metrics` system for protocol-level errors. Avoid using exceptions in the hot path. For application-level errors, use the `Logger` class.

### Protocol Compliance & Retries
*   **eBUS Protocol**: The `ebus` library handles eBUS protocol compliance, including NAK repetition and arbitration.
*   **Application Retries**: The `ebus` library's `Scheduler` provides configurable `max_send_attempts` and exponential backoff for high-level retries.
*   **Scan Filtering**: When implementing background tasks, only re-enqueue failed tasks if the failure was transient (e.g., arbitration loss). Whitelist successful `ebus::RequestResult` values rather than whitelisting `!success`.

## Key Components

*   **ebus Library (`lib/ebus`)**: The core eBUS protocol stack, handling bus communication, arbitration, message processing, and scheduling.
*   **Store (`src/Store.hpp`)**: Manages eBUS command configurations and their associated data, including persistence to LittleFS.
*   **Mqtt (`src/Mqtt.hpp`)**: Handles MQTT communication for publishing values, receiving commands, and Home Assistant auto-discovery.
*   **Cron (`src/Cron.hpp`)**: Manages scheduled eBUS write operations based on cron-like expressions.
*   **Http (`src/http.hpp`)**: Provides the web UI and API endpoints for configuration, control, and data display.
*   **ConfigManager (`src/ConfigManager.hpp`)**: Manages persistent configuration settings using NVS.
*   **WifiNetworkManager (`src/WifiNetworkManager.hpp`)**: Handles WiFi connectivity (STA and AP modes), mDNS, and static IP configuration.
*   **Adc (`src/Adc.hpp`)**: Manages ADC sampling and streaming for diagnostic purposes.
*   **UpgradeManager (`src/UpgradeManager.hpp`)**: Handles firmware updates via HTTP upload or URL.
*   **EspOtaManager (`src/EspOtaManager.hpp`)**: Handles firmware updates via ESP-OTA protocol (UDP).
*   **DNSServer (`src/DNSServer.h`)**: Provides DNS services for the captive portal in AP mode.
*   **Logger (`src/Logger.hpp`)**: Manages application logging to a circular buffer and serial output.
*   **AdapterVersion (`src/AdapterVersion.hpp`)**: Provides adapter hardware and software version information from eFuse.

## Project Structure

*   `include/`: Public headers for the main application.
*   `src/`: Source files for the main application components.
*   `static/`: Static web assets (HTML, CSS, JS) for the web UI.

## How to Submit Changes

1.  **Small Commits**: Keep your commits atomic and focused on a single change.
2.  **Naming**: Ensure all new files follow the `snake_case` naming rule.
3.  **Headers**: Ensure every new file starts with the standard project license header.
4.  **Pull Requests**: Submit your changes via a Pull Request. Ensure that all tests pass before submission.

