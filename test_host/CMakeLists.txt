cmake_minimum_required(VERSION 3.16.0)
project(esp_ebus_app_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Same platform define as the ebus library itself (it add_definitions(-DPOSIX)
# on Linux): BusConfig/Controller layouts depend on it, and mismatched
# defines between the lib and these TUs are an ODR violation (exit crash).
if(UNIX AND NOT APPLE)
    add_definitions(-DPOSIX)
endif()

# Project root is one level up from test_host/
set(PROJECT_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# ebus source: local working tree when the lib/ebus symlink is present,
# pinned FetchContent otherwise (CI has no symlink). Keep EBUS_PIN in sync
# with the lib_deps pin in platformio.ini.
set(EBUS_PIN 601414fc584457cdd1696fb88fd002ccded17cde)
if(EXISTS ${PROJECT_ROOT}/lib/ebus/CMakeLists.txt)
    set(EBUS_SOURCE_DIR ${PROJECT_ROOT}/lib/ebus)
    # No host utilities in test builds (faster configure/build).
    set(EBUS_BUILD_TOOLS OFF)
    add_subdirectory(${EBUS_SOURCE_DIR} ${CMAKE_BINARY_DIR}/ebus-lib)
else()
    include(FetchContent)
    FetchContent_Declare(
        ebus
        GIT_REPOSITORY https://github.com/yuhu-/ebus.git
        GIT_TAG ${EBUS_PIN}
    )
    # No host utilities in test builds (set before MakeAvailable).
    set(EBUS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(ebus)
    set(EBUS_SOURCE_DIR ${ebus_SOURCE_DIR})
endif()

# Catch2: always our own FetchContent (hermetic, version-pinned like ebus).
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(Catch2)

add_library(host_test_settings INTERFACE)
target_include_directories(host_test_settings INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/freertos
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/sys
    ${PROJECT_ROOT}/include
    ${EBUS_SOURCE_DIR}/include
)
target_compile_definitions(host_test_settings INTERFACE
    EBUS_INTERNAL=1
    EBUS_SIMULATION=0
    ESP32=1
    AUTO_VERSION="host-test"
    EBUS_CRON_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/cron_api_test.json"
    EBUS_COMMANDS_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/commands_api_test.json"
    EBUS_COMMANDS_TMP_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/commands_api_upload_test.json"
)
target_link_libraries(host_test_settings INTERFACE ebus)

add_library(system_test_support STATIC
    ${PROJECT_ROOT}/src/system/string_pool.cpp
)
target_link_libraries(system_test_support PUBLIC host_test_settings)

add_library(app_host_stubs STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/app/app_stub.cpp
)
target_link_libraries(app_host_stubs PUBLIC host_test_settings)

add_library(http_test_support STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/api/http_stubs.cpp
)
target_link_libraries(http_test_support PUBLIC host_test_settings)

add_library(config_test_support STATIC
    ${PROJECT_ROOT}/src/config/app_config.cpp
    ${PROJECT_ROOT}/src/config/app_config_loader.cpp
    ${PROJECT_ROOT}/src/config/config_manager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/config/nvs_mock.cpp
)
target_link_libraries(config_test_support PUBLIC
    host_test_settings
    app_host_stubs
    http_test_support
)

add_library(app_test_support STATIC
    ${PROJECT_ROOT}/src/app/command.cpp
    ${PROJECT_ROOT}/src/app/command_manager.cpp
    ${PROJECT_ROOT}/src/app/cron.cpp
    ${PROJECT_ROOT}/src/app/data_profile.cpp
    ${PROJECT_ROOT}/src/app/ebus_accessor.cpp
    ${PROJECT_ROOT}/src/app/ha_profile.cpp
    ${PROJECT_ROOT}/src/app/mqtt_ha.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/system/logger_stub.cpp
)
target_link_libraries(app_test_support PUBLIC
    host_test_settings
    config_test_support
    system_test_support
)

add_library(api_test_support STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/api/adc_stub.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/api/html_stubs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/api/status_api_stubs.cpp
)
target_link_libraries(api_test_support PUBLIC
    app_test_support
    config_test_support
    system_test_support
    http_test_support
)

add_executable(api_tests
    ${PROJECT_ROOT}/src/api/adc_api.cpp
    ${PROJECT_ROOT}/src/api/config_api.cpp
    ${PROJECT_ROOT}/src/api/commands_api.cpp
    ${PROJECT_ROOT}/src/api/cron_api.cpp
    ${PROJECT_ROOT}/src/api/devices_api.cpp
    ${PROJECT_ROOT}/src/api/logs_api.cpp
    ${PROJECT_ROOT}/src/api/metrics_api.cpp
    ${PROJECT_ROOT}/src/api/network_api.cpp
    ${PROJECT_ROOT}/src/api/status_api.cpp
    ${PROJECT_ROOT}/src/api/system_api.cpp
    ${PROJECT_ROOT}/src/api/values_api.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/api/values_api_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/api/adc_api_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/api/config_api_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/api/commands_api_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/api/cron_api_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/api/devices_api_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/api/logs_api_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/api/metrics_api_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/api/network_api_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/api/status_api_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/api/system_api_tests.cpp
)
target_link_libraries(api_tests PRIVATE
    api_test_support
    Catch2::Catch2WithMain
)

add_executable(app_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/app/command_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/command_manager_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/cron_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/data_profile_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/ha_profile_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/mqtt_ha_tests.cpp
)
target_link_libraries(app_tests PRIVATE
    app_test_support
    Catch2::Catch2WithMain
)

add_executable(config_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/config/app_config_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/config/config_manager_tests.cpp
)
target_link_libraries(config_tests PRIVATE
    config_test_support
    Catch2::Catch2WithMain
)

add_executable(network_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/network/wifi_tests.cpp
)
target_include_directories(network_tests PRIVATE
    ${PROJECT_ROOT}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
)
target_link_libraries(network_tests PRIVATE
    Catch2::Catch2WithMain
)

# Exercise the production Wi-Fi manager in both firmware profiles, replacing
# only the ESP-IDF boundary. Keep mocks isolated from API tests.
foreach(profile bridge internal)
    add_executable(wifi_manager_${profile}_tests
        ${PROJECT_ROOT}/src/network/wifi_network_manager.cpp
        ${PROJECT_ROOT}/src/config/app_config.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/network/wifi_manager_tests.cpp
    )
    target_include_directories(wifi_manager_${profile}_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks/wifi
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks
        ${PROJECT_ROOT}/include
        ${EBUS_SOURCE_DIR}/include
    )
    target_link_libraries(wifi_manager_${profile}_tests PRIVATE ebus Catch2::Catch2WithMain)
endforeach()
target_compile_definitions(wifi_manager_internal_tests PRIVATE EBUS_INTERNAL=1)

add_executable(system_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/system/string_pool_tests.cpp
)
target_link_libraries(system_tests PRIVATE
    system_test_support
    Catch2::Catch2WithMain
)

add_executable(http_utils_tests
    ${PROJECT_ROOT}/src/network/http_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/http_utils_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/http_utils/logger_stub.cpp
)
target_include_directories(http_utils_tests BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/http_utils
)
target_include_directories(http_utils_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${PROJECT_ROOT}/include
    ${EBUS_SOURCE_DIR}/include
)
target_link_libraries(http_utils_tests PRIVATE
    ebus
    Catch2::Catch2WithMain
)

add_executable(logger_tests
    ${PROJECT_ROOT}/src/system/logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/system/logger_tests.cpp
)
target_include_directories(logger_tests BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/logger
)
target_include_directories(logger_tests BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/logger/freertos
)
target_include_directories(logger_tests PRIVATE
    ${PROJECT_ROOT}/include
    ${EBUS_SOURCE_DIR}/include
)
target_compile_definitions(logger_tests PRIVATE LOGGER_MAX_ENTRIES=4)
target_link_libraries(logger_tests PRIVATE
    ebus
    Catch2::Catch2WithMain
)

add_executable(bridge_tests
    ${PROJECT_ROOT}/src/bridge/arbitration.cpp
    ${PROJECT_ROOT}/src/bridge/bus_type.cpp
    ${PROJECT_ROOT}/src/bridge/client.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge/arbitration_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/bridge/client_protocol_tests.cpp
)
target_include_directories(bridge_tests BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/bridge
)
target_include_directories(bridge_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${PROJECT_ROOT}/include
    ${EBUS_SOURCE_DIR}/include
)
target_compile_definitions(bridge_tests PRIVATE USE_ASYNCHRONOUS=0)
target_link_libraries(bridge_tests PRIVATE
    Catch2::Catch2WithMain
)

add_executable(identity_tests
    ${PROJECT_ROOT}/src/system/adapter_version.cpp
    ${PROJECT_ROOT}/src/system/device_identity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/system/adapter_version_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/system/device_identity_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/identity/identity_stubs.cpp
)
target_include_directories(identity_tests BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/identity
)
target_include_directories(identity_tests PRIVATE
    ${PROJECT_ROOT}/include
)
target_link_libraries(identity_tests PRIVATE
    Catch2::Catch2WithMain
)

enable_testing()
include(CTest)
# Per-case CTest entries: timeouts apply per case,
# failures pinpoint, same Catch.cmake module from our Catch2 sources.
# Off for sanitizer/valgrind runs (they drive the binary directly).
option(DISABLE_CATCH_DISCOVERY "Skip post-build test discovery" OFF)
if(NOT DISABLE_CATCH_DISCOVERY)
  include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
  catch_discover_tests(api_tests PROPERTIES RESOURCE_LOCK app_test_persistence)
  catch_discover_tests(app_tests PROPERTIES RESOURCE_LOCK app_test_persistence)
  catch_discover_tests(config_tests)
  catch_discover_tests(network_tests)
  catch_discover_tests(wifi_manager_bridge_tests TEST_PREFIX "bridge: ")
  catch_discover_tests(wifi_manager_internal_tests TEST_PREFIX "internal: ")
  catch_discover_tests(system_tests)
  catch_discover_tests(http_utils_tests)
  catch_discover_tests(logger_tests)
  catch_discover_tests(bridge_tests)
  catch_discover_tests(identity_tests)
endif()
