cmake_minimum_required(VERSION 3.10.0)
project(esp_ebus_app_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Project root is one level up from test_host/
set(PROJECT_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# Add mocks BEFORE app includes
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/freertos
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/sys
    ${PROJECT_ROOT}/include
    ${PROJECT_ROOT}/lib/ebus/include
)

# Use mock Logger instead of real one
set(APP_SOURCES
    ${PROJECT_ROOT}/src/command.cpp
    ${PROJECT_ROOT}/src/command_manager.cpp
    ${PROJECT_ROOT}/src/data_profile.cpp
    ${PROJECT_ROOT}/src/ha_profile.cpp
    ${PROJECT_ROOT}/src/string_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks/logger.cpp
)

# Test files
file(GLOB TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# Create test executable
add_executable(app_tests ${APP_SOURCES} ${TEST_SOURCES})

# Disable ebus library internal features not needed for tests
target_compile_definitions(app_tests PRIVATE
    EBUS_INTERNAL=1
    EBUS_SIMULATION=0
    ESP32=1
)

# Find and link Catch2 from the ebus library build
find_package(Catch2 REQUIRED PATHS ${PROJECT_ROOT}/lib/ebus/build/_deps/catch2-src)
target_link_libraries(app_tests PRIVATE
    ${PROJECT_ROOT}/lib/ebus/build/src/ebus/libebus.a
    Catch2::Catch2WithMain
)

enable_testing()
include(CTest)
