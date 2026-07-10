#pragma once

#include <memory>
#include <atomic>
#include <string>
#include <thread>
#include <chrono>

#include <httplib.h>

#include "drone_telemetry/database.hpp"
#include "drone_telemetry/telemetry_collector.hpp"

namespace drone_telemetry {

/// REST API server that serves telemetry data and provides drone control.
class TelemetryServer {
public:
  TelemetryServer(const Config& cfg,
                  std::shared_ptr<Database> db,
                  std::shared_ptr<ITelemetryCollector> collector);
  ~TelemetryServer();

  // No copy/move
  TelemetryServer(const TelemetryServer&) = delete;
  TelemetryServer& operator=(const TelemetryServer&) = delete;

  /// Start the HTTP server (blocking call).
  int run();

  /// Request graceful shutdown from another thread.
  void stop();

private:
  // ── Route handlers ──
  void handle_options(const httplib::Request& req, httplib::Response& res);
  void handle_health(const httplib::Request& req, httplib::Response& res);
  void handle_status(const httplib::Request& req, httplib::Response& res);
  void handle_latest_telemetry(const httplib::Request& req, httplib::Response& res);
  void handle_telemetry_history(const httplib::Request& req, httplib::Response& res);
  void handle_connect(const httplib::Request& req, httplib::Response& res);

  // ── Telemetry polling loop ──
  void polling_loop();

  // ── Add CORS headers to every response ──
  static void add_cors(httplib::Response& res);
  static void json_response(httplib::Response& res, const json& data, int status = 200);
  static void error_response(httplib::Response& res, int status, std::string_view message);

  // ── Members ──
  httplib::Server svr_;
  std::shared_ptr<Database> db_;
  std::shared_ptr<ITelemetryCollector> collector_;
  int port_;
  int poll_interval_ms_;

  std::atomic<bool> running_{false};
  std::atomic<TelemetryData> latest_data_;
  std::thread poll_thread_;
};

} // namespace drone_telemetry
