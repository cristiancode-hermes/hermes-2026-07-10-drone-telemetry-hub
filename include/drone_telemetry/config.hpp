#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <chrono>

namespace drone_telemetry {

/// Parsed configuration from CLI arguments and environment variables.
struct Config {
  int port = 8080;
  std::string db_path = "telemetry.db";
  std::string connection_url = "udp://:14540";
  int poll_interval_ms = 1000;
  bool mock_mode = true;
  std::string log_level = "info";

  /// Parse CLI arguments (argc/argv) and merge with env vars.
  static Config parse(int argc, char** argv);

  /// Print usage string to stderr.
  static void print_usage(std::string_view progname);
};

// ── Environment helper ──

inline std::optional<std::string> get_env(std::string_view name) {
  const char* val = std::getenv(name.data());
  if (!val) return std::nullopt;
  return std::string{val};
}

inline int parse_int(std::string_view s, int fallback) {
  int result = fallback;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), result);
  if (ec != std::errc{}) return fallback;
  return result;
}

// ── Inline definition (needed because Config::parse is non-template) ──
// Defined in config.cpp

} // namespace drone_telemetry
