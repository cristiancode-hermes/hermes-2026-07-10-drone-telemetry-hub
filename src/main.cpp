#include "drone_telemetry/config.hpp"
#include "drone_telemetry/database.hpp"
#include "drone_telemetry/telemetry_collector.hpp"
#include "drone_telemetry/server.hpp"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <cstdlib>
#include <memory>
#include <signal.h>

namespace {

std::unique_ptr<drone_telemetry::TelemetryServer> g_server;

void signal_handler(int sig) {
  spdlog::warn("Received signal {} — shutting down...", sig);
  if (g_server) {
    g_server->stop();
  }
}

void setup_signal_handlers() {
  struct sigaction sa{};
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT,  &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  // Ignore SIGPIPE to prevent crashes on broken HTTP connections
  signal(SIGPIPE, SIG_IGN);
}

} // anonymous namespace

int main(int argc, char** argv) {
  using namespace drone_telemetry;

  // ── Parse config ──
  auto cfg = Config::parse(argc, argv);

  // ── Setup logging ──
  auto log_level = spdlog::level::from_str(cfg.log_level);
  spdlog::set_level(log_level);
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
  spdlog::info("Drone Telemetry Hub v1.0.0 starting...");

  // ── Initialize database ──
  auto db = std::make_shared<Database>(cfg.db_path);
  auto init_result = db->init_tables();
  if (!init_result) {
    spdlog::error("Database init failed: {}", init_result.error().message);
    return 1;
  }

  // ── Create telemetry collector ──
  std::shared_ptr<ITelemetryCollector> collector;
  if (cfg.mock_mode) {
    spdlog::info("Using MOCK telemetry collector (simulated GPS data)");
    collector = create_mock_collector();
  } else {
#ifdef DRONE_TELEMETRY_USE_MAVSDK
    spdlog::info("Using MAVSDK telemetry collector (connection: {})", cfg.connection_url);
    collector = create_mavsdk_collector(cfg.connection_url);
#else
    spdlog::warn("MAVSDK support not compiled. Falling back to mock collector.");
    spdlog::warn("Rebuild with -DDRONE_TELEMETRY_USE_MAVSDK=ON to enable real drone connectivity.");
    collector = create_mock_collector();
#endif
  }

  // ── Setup signal handlers ──
  setup_signal_handlers();

  // ── Create and run server ──
  g_server = std::make_unique<TelemetryServer>(cfg, db, collector);
  int result = g_server->run();

  spdlog::info("Drone Telemetry Hub stopped (exit code: {})", result);
  return result;
}
