#include "drone_telemetry/config.hpp"

#include <iostream>
#include <cstdlib>
#include <expected>
#include <print>

namespace drone_telemetry {

Config Config::parse(int argc, char** argv) {
  Config cfg;

  // Environment overrides
  if (auto v = get_env("PORT"))           cfg.port = parse_int(*v, cfg.port);
  if (auto v = get_env("DB_PATH"))        cfg.db_path = *v;
  if (auto v = get_env("DRONE_CONNECTION_URL")) cfg.connection_url = *v;
  if (auto v = get_env("POLL_INTERVAL_MS")) cfg.poll_interval_ms = parse_int(*v, cfg.poll_interval_ms);
  if (auto v = get_env("MOCK_MODE"))      cfg.mock_mode = (*v == "1" || *v == "true");
  if (auto v = get_env("LOG_LEVEL"))      cfg.log_level = *v;

  // CLI overrides (later args win)
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};

    if (arg == "--port" && i + 1 < argc) {
      ++i;
      cfg.port = parse_int(argv[i], cfg.port);
    } else if (arg == "--db-path" && i + 1 < argc) {
      ++i;
      cfg.db_path = argv[i];
    } else if (arg == "--connection" && i + 1 < argc) {
      ++i;
      cfg.connection_url = argv[i];
    } else if (arg == "--poll-ms" && i + 1 < argc) {
      ++i;
      cfg.poll_interval_ms = parse_int(argv[i], cfg.poll_interval_ms);
    } else if (arg == "--mock") {
      cfg.mock_mode = true;
    } else if (arg == "--no-mock") {
      cfg.mock_mode = false;
    } else if (arg == "--log-level" && i + 1 < argc) {
      ++i;
      cfg.log_level = argv[i];
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0] ? argv[0] : "drone-telemetry-hub");
      std::exit(0);
    }
  }

  return cfg;
}

void Config::print_usage(std::string_view progname) {
  // NOTE: std::println with C++23 — literal braces escaped as {{}}
  std::println(stderr, R"(Drone Telemetry Hub v1.0.0
Usage: {} [OPTIONS]

Options:
  --port PORT           HTTP server port (default: 8080, env: PORT)
  --db-path PATH        SQLite database path (default: telemetry.db, env: DB_PATH)
  --connection URL      MAVSDK connection string (default: udp://:14540, env: DRONE_CONNECTION_URL)
  --poll-ms MS          Telemetry polling interval in ms (default: 1000, env: POLL_INTERVAL_MS)
  --mock                Enable mock telemetry data (default: true, env: MOCK_MODE=true)
  --no-mock             Disable mock mode (requires MAVSDK support)
  --log-level LEVEL     Log level: trace, debug, info, warn, error, critical (default: info, env: LOG_LEVEL)
  --help, -h            Show this help message

Examples:
  {} --port 9090 --db-path /data/flight.db --mock
  {} --connection udp://:14540 --no-mock --log-level debug
)",
    progname, progname, progname);
}

} // namespace drone_telemetry
