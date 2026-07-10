cmake_minimum_required(VERSION 3.28)
project(tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Catch2 comes from FetchContent in parent CMakeLists.txt
# We just need to add the test executable
add_executable(test_drone_telemetry test_main.cpp)
target_include_directories(test_drone_telemetry PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_drone_telemetry PRIVATE
  Catch2::Catch2WithMain
  httplib::httplib
  nlohmann_json::nlohmann_json
  spdlog::spdlog
  SQLite::SQLite3
)

# Manual test discovery (Catch2 module may not propagate from parent FetchContent)
include(CTest)
add_test(NAME test_drone_telemetry COMMAND test_drone_telemetry)
