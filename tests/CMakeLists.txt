cmake_minimum_required(VERSION 3.20)
project(jarolift_host_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# doctest 2.4.11 still declares cmake_minimum_required < 3.5
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
include(FetchContent)
FetchContent_Declare(
  doctest
  URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
  URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(doctest)

# Device-independent sources of the ESPHome component (no esphome/ includes).
set(COMPONENT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components/jarolift)
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS ${COMPONENT_DIR}/core_*.cpp)
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)

# Original dongle sources, compiled as test oracles with a minimal Arduino shim.
set(REFERENCE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/reference/KeeloqLib.cpp)
set_source_files_properties(${REFERENCE_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")

add_executable(host_tests main.cpp ${TEST_SOURCES} ${CORE_SOURCES} ${REFERENCE_SOURCES})
target_include_directories(host_tests PRIVATE ${COMPONENT_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/reference)
target_compile_definitions(host_tests PRIVATE ARDUINO=100)
target_link_libraries(host_tests PRIVATE doctest::doctest)
target_compile_options(host_tests PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_test(NAME host_tests COMMAND host_tests)
