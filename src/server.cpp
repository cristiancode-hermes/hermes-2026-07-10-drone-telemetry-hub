#include "drone_telemetry/server.hpp"
#include "drone_telemetry/config.hpp"

#include <spdlog/spdlog.h>

namespace drone_telemetry {

TelemetryServer::TelemetryServer(const Config& cfg,
                                 std::shared_ptr<Database> db,
                                 std::shared_ptr<ITelemetryCollector> collector)
  : db_(std::move(db))
  , collector_(std::move(collector))
  , port_(cfg.port)
  , poll_interval_ms_(cfg.poll_interval_ms)
{
  // CORS preflight
  svr_.Options(R"(.*)", [this](const auto& req, auto& res) { handle_options(req, res); });

  // Routes
  svr_.Get("/api/health",            [this](const auto& req, auto& res) { handle_health(req, res); });
  svr_.Get("/api/status",            [this](const auto& req, auto& res) { handle_status(req, res); });
  svr_.Get("/api/telemetry/latest",  [this](const auto& req, auto& res) { handle_latest_telemetry(req, res); });
  svr_.Get("/api/telemetry/history", [this](const auto& req, auto& res) { handle_telemetry_history(req, res); });
  svr_.Post("/api/connect",          [this](const auto& req, auto& res) { handle_connect(req, res); });

  // Exception handler
  svr_.set_exception_handler([](const auto&, auto& res, std::exception_ptr ep) {
    try { std::rethrow_exception(ep); }
    catch (const std::exception& e) {
      spdlog::error("Unhandled exception: {}", e.what());
      error_response(res, 500, std::format("Internal server error: {}", e.what()));
    }
  });

  // Logging
  svr_.set_logger([](const auto& req, const auto& res) {
    spdlog::info("{} {} → {} ({} bytes)",
                 req.method, req.path, res.status, res.body.size());
  });

  spdlog::info("Routes configured ({} endpoints)", 5);
}

TelemetryServer::~TelemetryServer() {
  stop();
}

int TelemetryServer::run() {
  // Start telemetry polling
  running_ = true;
  poll_thread_ = std::thread(&TelemetryServer::polling_loop, this);

  spdlog::info("Starting HTTP server on port {}", port_);
  if (!svr_.bind_to_port("0.0.0.0", port_)) {
    spdlog::error("Failed to bind to port {}", port_);
    running_ = false;
    if (poll_thread_.joinable()) poll_thread_.join();
    return 1;
  }

  svr_.listen_after_bind();
  return 0;
}

void TelemetryServer::stop() {
  running_ = false;
  svr_.stop();
  if (poll_thread_.joinable()) poll_thread_.join();
  spdlog::info("Server stopped");
}

// ── Route implementations ──

void TelemetryServer::handle_options(const httplib::Request&, httplib::Response& res) {
  add_cors(res);
  res.status = 204;
}

void TelemetryServer::handle_health(const httplib::Request&, httplib::Response& res) {
  json response = {
    {"status", "ok"},
    {"version", "1.0.0"},
    {"uptime", std::time(nullptr)},
    {"mock_mode", !collector_->is_connected()}
  };
  json_response(res, response);
}

void TelemetryServer::handle_status(const httplib::Request&, httplib::Response& res) {
  auto count = db_->count_records();
  json response = {
    {"connected", collector_->is_connected()},
    {"telemetry_count", count ? *count : 0},
    {"db_path", "internal"},
    {"poll_interval_ms", poll_interval_ms_}
  };
  json_response(res, response);
}

void TelemetryServer::handle_latest_telemetry(const httplib::Request&, httplib::Response& res) {
  auto result = db_->get_latest_telemetry();
  if (!result) {
    error_response(res, 500, result.error().message);
    return;
  }

  if (result->has_value()) {
    json_response(res, result->value().to_json());
  } else {
    json_response(res, json{{"message", "No telemetry data available"}});
  }
}

void TelemetryServer::handle_telemetry_history(const httplib::Request& req, httplib::Response& res) {
  int page = 1, per_page = 20;
  if (req.params.contains("page"))     page = parse_int(req.params.at("page"), page);
  if (req.params.contains("per_page")) per_page = parse_int(req.params.at("per_page"), per_page);

  std::optional<std::chrono::system_clock::time_point> since;
  if (req.params.contains("since")) {
    int64_t ts = parse_int(req.params.at("since"), 0);
    since = std::chrono::system_clock::time_point{std::chrono::seconds{ts}};
  }

  auto records = db_->query_telemetry(page, per_page, since);
  if (!records) {
    error_response(res, 500, records.error().message);
    return;
  }

  auto count = db_->count_records();
  int64_t total = count ? *count : 0;
  int total_pages = std::max(1, static_cast<int>((total + per_page - 1) / per_page));

  json data = json::array();
  for (const auto& rec : *records) {
    data.push_back(rec.to_json());
  }

  json response = {
    {"data", std::move(data)},
    {"page", page},
    {"per_page", per_page},
    {"total", total},
    {"total_pages", total_pages}
  };
  json_response(res, response);
}

void TelemetryServer::handle_connect(const httplib::Request&, httplib::Response& res) {
  if (collector_->is_connected()) {
    json_response(res, {{"message", "Already connected"}});
    return;
  }

  auto result = collector_->connect();
  if (!result) {
    error_response(res, 502, std::format("Connection failed: {}", result.error()));
    return;
  }

  json_response(res, {{"message", "Connected successfully"}});
}

// ── Telemetry polling loop ──

void TelemetryServer::polling_loop() {
  spdlog::info("Telemetry polling started (interval: {}ms)", poll_interval_ms_);

  // Try to connect
  if (!collector_->is_connected()) {
    auto result = collector_->connect();
    if (!result) {
      spdlog::warn("Initial connection failed: {} (will retry)", result.error());
    }
  }

  while (running_) {
    if (collector_->is_connected()) {
      auto data = collector_->poll();
      if (data) {
        latest_data_.store(*data);

        // Store to database
        TelemetryRecord record;
        record.latitude_deg          = data->latitude_deg;
        record.longitude_deg         = data->longitude_deg;
        record.absolute_altitude_m   = data->absolute_altitude_m;
        record.relative_altitude_m   = data->relative_altitude_m;
        record.roll_deg              = data->roll_deg;
        record.pitch_deg             = data->pitch_deg;
        record.yaw_deg               = data->yaw_deg;
        record.voltage_v             = data->voltage_v;
        record.battery_remaining_pct = data->battery_remaining_pct;
        record.flight_mode           = data->flight_mode;
        record.timestamp             = data->timestamp;

        auto insert_result = db_->insert_telemetry(record);
        if (!insert_result) {
          spdlog::error("Failed to store telemetry: {}", insert_result.error().message);
        }
      } else {
        spdlog::warn("Telemetry poll failed: {}", data.error());
      }
    } else {
      // Try reconnecting
      auto result = collector_->connect();
      if (!result) {
        spdlog::debug("Reconnect attempt failed: {}", result.error());
      }
    }

    // Sleep for the poll interval with early wakeup on shutdown
    for (int i = 0; i < poll_interval_ms_ / 100 && running_; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  spdlog::info("Telemetry polling stopped");
}

// ── Static helpers ──

void TelemetryServer::add_cors(httplib::Response& res) {
  res.set_header("Access-Control-Allow-Origin", "*");
  res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
}

void TelemetryServer::json_response(httplib::Response& res, const json& data, int status) {
  add_cors(res);
  res.status = status;
  res.set_content(data.dump(2), "application/json");
}

void TelemetryServer::error_response(httplib::Response& res, int status, std::string_view message) {
  json err = {
    {"error", true},
    {"code", status},
    {"message", message}
  };
  json_response(res, err, status);
}

} // namespace drone_telemetry
