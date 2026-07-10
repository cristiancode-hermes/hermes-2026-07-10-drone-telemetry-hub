#include "drone_telemetry/telemetry_collector.hpp"

#include <random>
#include <cmath>
#include <thread>
#include <spdlog/spdlog.h>

namespace drone_telemetry {

// ═══════════════════════════════════════════════════════
// Mock telemetry collector — generates realistic data
// ═══════════════════════════════════════════════════════

class MockCollector final : public ITelemetryCollector {
public:
  MockCollector() : rng_(std::random_device{}()) {
    // Simulate Barcelona area as starting point
    lat_ = 41.3874;
    lon_ = 2.1686;
    alt_rel_ = 50.0;
    alt_abs_ = 50.0 + 12.0; // + geoid height approx
    start_time_ = std::chrono::system_clock::now();
    spdlog::info("MockCollector initialized at ({:.4f}, {:.4f})", lat_, lon_);
  }

  std::expected<TelemetryData, std::string> poll() override {
    if (!connected_) {
      return std::unexpected("Mock collector not connected");
    }

    simulate_step();
    return current_data();
  }

  bool is_connected() const override { return connected_; }

  std::expected<void, std::string> connect() override {
    connected_ = true;
    spdlog::info("MockCollector connected");
    return {};
  }

  void disconnect() override {
    connected_ = false;
    spdlog::info("MockCollector disconnected");
  }

private:
  TelemetryData current_data() {
    TelemetryData d;
    d.latitude_deg          = lat_;
    d.longitude_deg         = lon_;
    d.absolute_altitude_m   = alt_abs_;
    d.relative_altitude_m   = alt_rel_;
    d.roll_deg              = roll_;
    d.pitch_deg             = pitch_;
    d.yaw_deg               = yaw_;
    d.voltage_v             = voltage_;
    d.battery_remaining_pct = battery_pct_;
    d.flight_mode           = flight_mode_;
    d.timestamp             = std::chrono::system_clock::now();
    return d;
  }

  void simulate_step() {
    std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
    std::uniform_real_distribution<float> small_noise(-0.02f, 0.02f);
    std::uniform_real_distribution<float> alt_drift(-0.1f, 0.1f);

    auto now = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      now - start_time_).count();

    // Simulate a simple flight pattern: figure-8 or circular
    double t = elapsed * 0.02;
    lat_ += 0.0001 * std::sin(t * 0.7) + small_noise(rng_);
    lon_ += 0.0001 * std::cos(t * 0.5) + small_noise(rng_);
    alt_rel_ += alt_drift(rng_);
    alt_abs_ = alt_rel_ + 12.0;

    // Keep altitude reasonable
    if (alt_rel_ < 5.0) alt_rel_ = 5.0;
    if (alt_rel_ > 150.0) alt_rel_ = 150.0;

    // Attitude
    roll_  = 2.0f * std::sin(static_cast<float>(t * 0.3f)) + noise(rng_);
    pitch_ = 1.5f * std::cos(static_cast<float>(t * 0.2f)) + noise(rng_);
    yaw_   = static_cast<float>(std::fmod(t * 5.0, 360.0));

    // Battery slowly drains
    battery_pct_ = std::max(0.0f, 100.0f - static_cast<float>(elapsed) * 0.005f);
    voltage_ = 12.5f + (battery_pct_ / 100.0f) * 4.0f;

    // Flight mode transitions
    if (elapsed < 5)       flight_mode_ = "TAKEOFF";
    else if (elapsed < 60) flight_mode_ = "HOLD";
    else if (elapsed < 120) flight_mode_ = "MISSION";
    else if (elapsed < 180) flight_mode_ = "RTL";
    else                    flight_mode_ = "LAND";
  }

  std::mt19937 rng_;
  std::chrono::system_clock::time_point start_time_;
  bool connected_ = false;

  // State
  double lat_ = 41.3874;
  double lon_ = 2.1686;
  double alt_rel_ = 50.0;
  double alt_abs_ = 62.0;
  float roll_ = 0.0f, pitch_ = 0.0f, yaw_ = 0.0f;
  float voltage_ = 16.5f;
  float battery_pct_ = 100.0f;
  std::string flight_mode_ = "HOLD";
};

std::unique_ptr<ITelemetryCollector> create_mock_collector() {
  return std::make_unique<MockCollector>();
}

// ═══════════════════════════════════════════════════════
// MAVSDK collector (conditional compilation)
// ═══════════════════════════════════════════════════════

#ifdef DRONE_TELEMETRY_USE_MAVSDK

#include <mavsdk/mavsdk.h>
#include <mavsdk/plugins/telemetry/telemetry.h>

class MavsdkCollector final : public ITelemetryCollector {
public:
  explicit MavsdkCollector(std::string connection_url)
    : connection_url_(std::move(connection_url)) {}

  ~MavsdkCollector() override { disconnect(); }

  std::expected<TelemetryData, std::string> poll() override {
    if (!telemetry_ || !system_) {
      return std::unexpected("MAVSDK not initialized");
    }

    // Fetch latest from MAVSDK
    auto pos = telemetry_->position();
    auto euler = telemetry_->attitude_euler();
    auto batt = telemetry_->battery();
    auto flight_mode = telemetry_->flight_mode();

    TelemetryData data;
    data.latitude_deg          = pos.latitude_deg;
    data.longitude_deg         = pos.longitude_deg;
    data.absolute_altitude_m   = pos.absolute_altitude_m;
    data.relative_altitude_m   = pos.relative_altitude_m;
    data.roll_deg              = euler.roll_deg;
    data.pitch_deg             = euler.pitch_deg;
    data.yaw_deg               = euler.yaw_deg;
    data.voltage_v             = batt.voltage_v;
    data.battery_remaining_pct = batt.remaining_percent * 100.0f;
    data.timestamp             = std::chrono::system_clock::now();

    switch (flight_mode) {
      case mavsdk::Telemetry::FlightMode::Ready:         data.flight_mode = "READY"; break;
      case mavsdk::Telemetry::FlightMode::Takeoff:       data.flight_mode = "TAKEOFF"; break;
      case mavsdk::Telemetry::FlightMode::Hold:          data.flight_mode = "HOLD"; break;
      case mavsdk::Telemetry::FlightMode::Mission:       data.flight_mode = "MISSION"; break;
      case mavsdk::Telemetry::FlightMode::ReturnToLaunch: data.flight_mode = "RTL"; break;
      case mavsdk::Telemetry::FlightMode::Land:          data.flight_mode = "LAND"; break;
      case mavsdk::Telemetry::FlightMode::Offboard:      data.flight_mode = "OFFBOARD"; break;
      case mavsdk::Telemetry::FlightMode::FollowMe:      data.flight_mode = "FOLLOW_ME"; break;
      default:                                           data.flight_mode = "UNKNOWN"; break;
    }

    return data;
  }

  bool is_connected() const override {
    return system_.has_value();
  }

  std::expected<void, std::string> connect() override {
    using namespace mavsdk;

    if (system_.has_value()) {
      return {}; // already connected
    }

    auto conn_result = mavsdk_.add_any_connection(connection_url_);
    if (conn_result != ConnectionResult::Success) {
      return std::unexpected(
        std::format("Failed to connect to '{}': {}",
                    connection_url_, magic_enum_to_string(conn_result)));
    }

    auto system = mavsdk_.first_autopilot(std::chrono::seconds(10));
    if (!system) {
      return std::unexpected("No autopilot detected within 10 seconds");
    }

    system_ = system;
    telemetry_ = std::make_unique<mavsdk::Telemetry>(system.value());
    spdlog::info("MAVSDK connected to '{}'", connection_url_);
    return {};
  }

  void disconnect() override {
    telemetry_.reset();
    system_.reset();
    spdlog::info("MAVSDK disconnected");
  }

private:
  std::string connection_url_;
  mavsdk::Mavsdk mavsdk_;
  std::optional<std::shared_ptr<mavsdk::System>> system_;
  std::unique_ptr<mavsdk::Telemetry> telemetry_;
};

std::unique_ptr<ITelemetryCollector> create_mavsdk_collector(std::string connection_url) {
  return std::make_unique<MavsdkCollector>(std::move(connection_url));
}

#endif // DRONE_TELEMETRY_USE_MAVSDK

} // namespace drone_telemetry
