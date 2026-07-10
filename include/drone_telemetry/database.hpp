#pragma once

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <sqlite3.h>
#include <nlohmann/json.hpp>

namespace drone_telemetry {

using json = nlohmann::json;

/// Error type for database operations.
struct DbError {
  int code;
  std::string message;
};

template <typename T>
using DbResult = std::expected<T, DbError>;

/// A single telemetry data point stored in the database.
struct TelemetryRecord {
  int64_t id = 0;
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

  /// Serialize to JSON.
  [[nodiscard]] json to_json() const;

  /// Deserialize from JSON row.
  static TelemetryRecord from_json(const json& j);
};

/// RAII wrapper over SQLite3 with prepared statements.
class Database {
public:
  explicit Database(std::string_view path);
  ~Database();

  // No copy — RAII owns sqlite3*
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;

  // Move allowed
  Database(Database&& other) noexcept
    : db_(std::exchange(other.db_, nullptr)) {}

  Database& operator=(Database&& other) noexcept {
    if (this != &other) {
      close();
      db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
  }

  /// Create tables if they don't exist.
  [[nodiscard]] DbResult<void> init_tables();

  /// Insert a telemetry record. Returns the new row ID.
  [[nodiscard]] DbResult<int64_t> insert_telemetry(const TelemetryRecord& record);

  /// Query telemetry history with pagination.
  /// @param page 1-indexed page number.
  /// @param per_page records per page (max 1000).
  /// @param since_ts optional minimum timestamp filter.
  [[nodiscard]] DbResult<std::vector<TelemetryRecord>>
  query_telemetry(int page = 1, int per_page = 20,
                  std::optional<std::chrono::system_clock::time_point> since_ts = std::nullopt);

  /// Get the most recent telemetry record.
  [[nodiscard]] DbResult<std::optional<TelemetryRecord>> get_latest_telemetry();

  /// Get total record count (for pagination).
  [[nodiscard]] DbResult<int64_t> count_records();

private:
  sqlite3* db_ = nullptr;

  void close();

  /// Execute a raw SQL statement (for DDL).
  [[nodiscard]] DbResult<void> execute_raw(std::string_view sql);

  /// Bind timestamp to prepared statement at given index.
  static void bind_time(sqlite3_stmt* stmt, int idx,
                        const std::chrono::system_clock::time_point& tp);
};

} // namespace drone_telemetry
