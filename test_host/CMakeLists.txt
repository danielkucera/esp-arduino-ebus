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
set(EBUS_PIN 3bf6b6a34a47ed8b5877563cdd97bb55b1baf71f)
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

# Add mocks BEFORE app includes
# NOTE: ebus exposes its headers repository-relative, plus the explicit
# dir below (harmless duplicate, guards older pins).
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/freertos
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/sys
    ${PROJECT_ROOT}/include
    ${EBUS_SOURCE_DIR}/include
)

# Use mock Logger instead of real one
set(APP_SOURCES
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
    ${PROJECT_ROOT}/src/app/command.cpp
    ${PROJECT_ROOT}/src/app/command_manager.cpp
    ${PROJECT_ROOT}/src/app/cron.cpp
    ${PROJECT_ROOT}/src/app/data_profile.cpp
    ${PROJECT_ROOT}/src/app/ebus_accessor.cpp
    ${PROJECT_ROOT}/src/app/ha_profile.cpp
    ${PROJECT_ROOT}/src/app/mqtt_ha.cpp
    ${PROJECT_ROOT}/src/config/app_config.cpp
    ${PROJECT_ROOT}/src/config/app_config_loader.cpp
    ${PROJECT_ROOT}/src/config/config_manager.cpp
    ${PROJECT_ROOT}/src/system/string_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/adc_stub.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/app_stub.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/html_stubs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/http_stubs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/logger.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/nvs_mock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/status_api_stubs.cpp
)

# Test files mirror src/ (explicit: adding a test file requires listing it).
set(TEST_SOURCES
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
    ${CMAKE_CURRENT_SOURCE_DIR}/app/command_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/command_manager_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/cron_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/data_profile_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/ha_profile_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/app/mqtt_ha_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/config/app_config_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/config/config_manager_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/network/wifi_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/system/string_pool_tests.cpp
)

# Create test executable
add_executable(app_tests ${APP_SOURCES} ${TEST_SOURCES})

# Disable ebus library internal features not needed for tests
target_compile_definitions(app_tests PRIVATE
    EBUS_INTERNAL=1
    EBUS_SIMULATION=0
    ESP32=1
    AUTO_VERSION="host-test"
    EBUS_CRON_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/cron_api_test.json"
    EBUS_COMMANDS_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/commands_api_test.json"
    EBUS_COMMANDS_TMP_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/commands_api_upload_test.json"
)

target_link_libraries(app_tests PRIVATE
    ebus
    Catch2::Catch2WithMain
)

add_executable(http_utils_tests
    ${PROJECT_ROOT}/src/network/http_utils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/http_utils/http_utils_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/http_utils/logger_stub.cpp
)
target_include_directories(http_utils_tests BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/http_utils/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/http_utils
)
target_link_libraries(http_utils_tests PRIVATE
    ebus
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
  catch_discover_tests(app_tests)
  catch_discover_tests(http_utils_tests)
endif()
