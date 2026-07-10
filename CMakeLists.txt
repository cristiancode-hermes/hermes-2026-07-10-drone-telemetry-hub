cmake_minimum_required(VERSION 3.28)
project(drone-telemetry-hub VERSION 1.0.0 LANGUAGES CXX)

# ── C++ Standard ──
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Options ──
option(DRONE_TELEMETRY_USE_MAVSDK "Enable real MAVSDK drone connection (requires MAVSDK)" OFF)
option(BUILD_TESTS "Build unit and integration tests" ON)

# ── Dependencies ──
include(FetchContent)

# cpp-httplib (header-only HTTP/HTTPS server)
FetchContent_Declare(cpp-httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.18.2
)
set(HTTPLIB_USE_NONBLOCKING_IF_OPENSSL OFF CACHE BOOL "")
FetchContent_MakeAvailable(cpp-httplib)

# nlohmann_json (header-only JSON library)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# spdlog (header-only logging)
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.15.1
)
FetchContent_MakeAvailable(spdlog)

# SQLite3 (system package)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(SQLITE3 sqlite3)
endif()
if(NOT SQLITE3_FOUND)
  find_package(SQLite3 REQUIRED)
endif()

# MAVSDK (optional — for real drone connectivity)
if(DRONE_TELEMETRY_USE_MAVSDK)
  find_package(MAVSDK REQUIRED)
  message(STATUS "MAVSDK found — real drone connectivity enabled")
else()
  message(STATUS "MAVSDK disabled — using simulated telemetry data")
endif()

# ── Sources ──
file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS include/*.hpp include/*.h)

add_executable(drone-telemetry-hub ${SOURCES} ${HEADERS})
target_include_directories(drone-telemetry-hub PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(drone-telemetry-hub PRIVATE
  httplib::httplib
  nlohmann_json::nlohmann_json
  spdlog::spdlog
  SQLite::SQLite3
)

if(DRONE_TELEMETRY_USE_MAVSDK)
  target_compile_definitions(drone-telemetry-hub PRIVATE DRONE_TELEMETRY_USE_MAVSDK)
  target_link_libraries(drone-telemetry-hub PRIVATE MAVSDK::mavsdk)
endif()

# ── Compiler warnings ──
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(drone-telemetry-hub PRIVATE
    -Wall -Wextra -Wpedantic -Werror=return-type -Wno-unused-parameter
  )
endif()

# ── Tests ──
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ── Install ──
install(TARGETS drone-telemetry-hub RUNTIME DESTINATION bin)

# ── Summary ──
message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
message(STATUS " Drone Telemetry Hub ${PROJECT_VERSION}")
message(STATUS " C++ Standard:     ${CMAKE_CXX_STANDARD}")
message(STATUS " MAVSDK support:   ${DRONE_TELEMETRY_USE_MAVSDK}")
message(STATUS " Unit tests:       ${BUILD_TESTS}")
message(STATUS "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
