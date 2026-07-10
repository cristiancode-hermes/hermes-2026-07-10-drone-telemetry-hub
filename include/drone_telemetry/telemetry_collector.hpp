#pragma once

#include <expected>
#include <memory>
#include <string>
#include <chrono>

namespace drone_telemetry {

/// Current telemetry snapshot from the drone.
struct TelemetryData {
  double latitude_deg = 0.0;
  double longitude_deg = 0.0;
  double absolute_altitude_m = 0.0;
  double relative_altitude_m = 0.0;
  float roll_deg = 0.0f;
  float pitch_deg = 0.0f;
  float yaw_deg = 0.0f;
  float voltage_v = 0.0f;
  float battery_remaining_pct = 0.0f;
  std::string flight_mode;
  std::chrono::system_clock::time_point timestamp;
};

/// Abstract interface for telemetry collection.
class ITelemetryCollector {
public:
  virtual ~ITelemetryCollector() = default;

  /// Poll for the latest telemetry data. Returns the data or an error message.
  [[nodiscard]] virtual std::expected<TelemetryData, std::string> poll() = 0;

  /// Whether the collector is currently connected to a data source.
  [[nodiscard]] virtual bool is_connected() const = 0;

  /// Attempt to connect/reconnect.
  [[nodiscard]] virtual std::expected<void, std::string> connect() = 0;

  /// Disconnect gracefully.
  virtual void disconnect() = 0;
};

// ── Factory ──

/// Create a mock telemetry collector that generates simulated GPS data.
[[nodiscard]] std::unique_ptr<ITelemetryCollector> create_mock_collector();

#ifdef DRONE_TELEMETRY_USE_MAVSDK
/// Create a real MAVSDK-based telemetry collector.
/// @param connection_url e.g. "udp://:14540"
[[nodiscard]] std::unique_ptr<ITelemetryCollector> create_mavsdk_collector(
    std::string connection_url);
#endif

} // namespace drone_telemetry
