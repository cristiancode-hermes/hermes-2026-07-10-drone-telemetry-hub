#include "drone_telemetry/database.hpp"

#include <cstring>
#include <format>
#include <spdlog/spdlog.h>

namespace drone_telemetry {

// ── Helpers ──

namespace {

/// Convert system_clock::time_point to Unix epoch seconds (integer).
int64_t to_unix_epoch(const std::chrono::system_clock::time_point& tp) {
  return std::chrono::duration_cast<std::chrono::seconds>(
    tp.time_since_epoch()
  ).count();
}

std::chrono::system_clock::time_point from_unix_epoch(int64_t seconds) {
  return std::chrono::system_clock::time_point{
    std::chrono::seconds{seconds}
  };
}

} // anonymous namespace

// ── JSON serialization ──

json TelemetryRecord::to_json() const {
  return {
    {"id", id},
    {"latitude_deg", latitude_deg},
    {"longitude_deg", longitude_deg},
    {"absolute_altitude_m", absolute_altitude_m},
    {"relative_altitude_m", relative_altitude_m},
    {"roll_deg", roll_deg},
    {"pitch_deg", pitch_deg},
    {"yaw_deg", yaw_deg},
    {"voltage_v", voltage_v},
    {"battery_remaining_pct", battery_remaining_pct},
    {"flight_mode", flight_mode},
    {"timestamp", to_unix_epoch(timestamp)}
  };
}

TelemetryRecord TelemetryRecord::from_json(const json& j) {
  TelemetryRecord r;
  if (j.contains("id"))    r.id = j["id"].get<int64_t>();
  if (j.contains("latitude_deg"))  r.latitude_deg = j["latitude_deg"].get<double>();
  if (j.contains("longitude_deg")) r.longitude_deg = j["longitude_deg"].get<double>();
  if (j.contains("absolute_altitude_m")) r.absolute_altitude_m = j["absolute_altitude_m"].get<double>();
  if (j.contains("relative_altitude_m")) r.relative_altitude_m = j["relative_altitude_m"].get<double>();
  if (j.contains("roll_deg"))   r.roll_deg = j["roll_deg"].get<float>();
  if (j.contains("pitch_deg"))  r.pitch_deg = j["pitch_deg"].get<float>();
  if (j.contains("yaw_deg"))    r.yaw_deg = j["yaw_deg"].get<float>();
  if (j.contains("voltage_v"))  r.voltage_v = j["voltage_v"].get<float>();
  if (j.contains("battery_remaining_pct")) r.battery_remaining_pct = j["battery_remaining_pct"].get<float>();
  if (j.contains("flight_mode")) r.flight_mode = j["flight_mode"].get<std::string>();
  if (j.contains("timestamp"))  r.timestamp = from_unix_epoch(j["timestamp"].get<int64_t>());
  return r;
}

// ── Database ──

Database::Database(std::string_view path) {
  int rc = sqlite3_open(path.data(), &db_);
  if (rc != SQLITE_OK) {
    std::string msg = std::format("Failed to open database '{}': {}",
                                  path, sqlite3_errmsg(db_));
    sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error(msg);
  }
  spdlog::info("Opened database: {}", path);
}

Database::~Database() { close(); }

void Database::close() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
    spdlog::debug("Database connection closed");
  }
}

DbResult<void> Database::execute_raw(std::string_view sql) {
  char* err_msg = nullptr;
  int rc = sqlite3_exec(db_, sql.data(), nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    std::string msg = err_msg;
    sqlite3_free(err_msg);
    return std::unexpected(DbError{500, std::move(msg)});
  }
  return {};
}

void Database::bind_time(sqlite3_stmt* stmt, int idx,
                         const std::chrono::system_clock::time_point& tp) {
  sqlite3_bind_int64(stmt, idx, to_unix_epoch(tp));
}

DbResult<void> Database::init_tables() {
  constexpr std::string_view sql = R"(
    CREATE TABLE IF NOT EXISTS telemetry (
      id                  INTEGER PRIMARY KEY AUTOINCREMENT,
      latitude_deg        REAL NOT NULL,
      longitude_deg       REAL NOT NULL,
      absolute_altitude_m REAL NOT NULL,
      relative_altitude_m REAL NOT NULL,
      roll_deg            REAL NOT NULL,
      pitch_deg           REAL NOT NULL,
      yaw_deg             REAL NOT NULL,
      voltage_v           REAL NOT NULL,
      battery_pct         REAL NOT NULL,
      flight_mode         TEXT NOT NULL DEFAULT 'unknown',
      timestamp           INTEGER NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_telemetry_ts ON telemetry(timestamp DESC);
  )";

  auto result = execute_raw(sql);
  if (result) {
    spdlog::info("Database tables initialized");
  } else {
    spdlog::error("Failed to initialize tables: {}", result.error().message);
  }
  return result;
}

DbResult<int64_t> Database::insert_telemetry(const TelemetryRecord& record) {
  constexpr std::string_view sql = R"(
    INSERT INTO telemetry
      (latitude_deg, longitude_deg, absolute_altitude_m, relative_altitude_m,
       roll_deg, pitch_deg, yaw_deg, voltage_v, battery_pct, flight_mode, timestamp)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
  )";

  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
  if (rc != SQLITE_OK) {
    return std::unexpected(DbError{500, sqlite3_errmsg(db_)});
  }

  sqlite3_bind_double(stmt, 1, record.latitude_deg);
  sqlite3_bind_double(stmt, 2, record.longitude_deg);
  sqlite3_bind_double(stmt, 3, record.absolute_altitude_m);
  sqlite3_bind_double(stmt, 4, record.relative_altitude_m);
  sqlite3_bind_double(stmt, 5, record.roll_deg);
  sqlite3_bind_double(stmt, 6, record.pitch_deg);
  sqlite3_bind_double(stmt, 7, record.yaw_deg);
  sqlite3_bind_double(stmt, 8, record.voltage_v);
  sqlite3_bind_double(stmt, 9, record.battery_remaining_pct);
  sqlite3_bind_text(stmt, 10, record.flight_mode.data(),
                    static_cast<int>(record.flight_mode.size()), SQLITE_TRANSIENT);
  bind_time(stmt, 11, record.timestamp);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    return std::unexpected(DbError{500, sqlite3_errmsg(db_)});
  }

  int64_t row_id = sqlite3_last_insert_rowid(db_);
  spdlog::trace("Inserted telemetry record id={}", row_id);
  return row_id;
}

DbResult<std::vector<TelemetryRecord>>
Database::query_telemetry(int page, int per_page,
                          std::optional<std::chrono::system_clock::time_point> since_ts) {
  if (page < 1) page = 1;
  if (per_page < 1) per_page = 20;
  if (per_page > 1000) per_page = 1000;
  int offset = (page - 1) * per_page;

  std::string sql = R"(
    SELECT id, latitude_deg, longitude_deg, absolute_altitude_m, relative_altitude_m,
           roll_deg, pitch_deg, yaw_deg, voltage_v, battery_pct, flight_mode, timestamp
    FROM telemetry
  )";

  if (since_ts.has_value()) {
    sql += std::format(" WHERE timestamp >= {}", to_unix_epoch(*since_ts));
  }

  sql += " ORDER BY timestamp DESC LIMIT ? OFFSET ?";

  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
  if (rc != SQLITE_OK) {
    return std::unexpected(DbError{500, sqlite3_errmsg(db_)});
  }

  sqlite3_bind_int(stmt, 1, per_page);
  sqlite3_bind_int(stmt, 2, offset);

  std::vector<TelemetryRecord> records;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    TelemetryRecord r;
    r.id                   = sqlite3_column_int64(stmt, 0);
    r.latitude_deg         = sqlite3_column_double(stmt, 1);
    r.longitude_deg        = sqlite3_column_double(stmt, 2);
    r.absolute_altitude_m  = sqlite3_column_double(stmt, 3);
    r.relative_altitude_m  = sqlite3_column_double(stmt, 4);
    r.roll_deg             = static_cast<float>(sqlite3_column_double(stmt, 5));
    r.pitch_deg            = static_cast<float>(sqlite3_column_double(stmt, 6));
    r.yaw_deg              = static_cast<float>(sqlite3_column_double(stmt, 7));
    r.voltage_v            = static_cast<float>(sqlite3_column_double(stmt, 8));
    r.battery_remaining_pct = static_cast<float>(sqlite3_column_double(stmt, 9));
    if (auto text = sqlite3_column_text(stmt, 10))
      r.flight_mode = reinterpret_cast<const char*>(text);
    r.timestamp = from_unix_epoch(sqlite3_column_int64(stmt, 11));
    records.push_back(std::move(r));
  }

  sqlite3_finalize(stmt);
  return records;
}

DbResult<std::optional<TelemetryRecord>> Database::get_latest_telemetry() {
  constexpr std::string_view sql = R"(
    SELECT id, latitude_deg, longitude_deg, absolute_altitude_m, relative_altitude_m,
           roll_deg, pitch_deg, yaw_deg, voltage_v, battery_pct, flight_mode, timestamp
    FROM telemetry ORDER BY timestamp DESC LIMIT 1
  )";

  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
  if (rc != SQLITE_OK) {
    return std::unexpected(DbError{500, sqlite3_errmsg(db_)});
  }

  std::optional<TelemetryRecord> result;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    TelemetryRecord r;
    r.id                   = sqlite3_column_int64(stmt, 0);
    r.latitude_deg         = sqlite3_column_double(stmt, 1);
    r.longitude_deg        = sqlite3_column_double(stmt, 2);
    r.absolute_altitude_m  = sqlite3_column_double(stmt, 3);
    r.relative_altitude_m  = sqlite3_column_double(stmt, 4);
    r.roll_deg             = static_cast<float>(sqlite3_column_double(stmt, 5));
    r.pitch_deg            = static_cast<float>(sqlite3_column_double(stmt, 6));
    r.yaw_deg              = static_cast<float>(sqlite3_column_double(stmt, 7));
    r.voltage_v            = static_cast<float>(sqlite3_column_double(stmt, 8));
    r.battery_remaining_pct = static_cast<float>(sqlite3_column_double(stmt, 9));
    if (auto text = sqlite3_column_text(stmt, 10))
      r.flight_mode = reinterpret_cast<const char*>(text);
    r.timestamp = from_unix_epoch(sqlite3_column_int64(stmt, 11));
    result = std::move(r);
  }

  sqlite3_finalize(stmt);
  return result;
}

DbResult<int64_t> Database::count_records() {
  constexpr std::string_view sql = "SELECT COUNT(*) FROM telemetry";

  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
  if (rc != SQLITE_OK) {
    return std::unexpected(DbError{500, sqlite3_errmsg(db_)});
  }

  int64_t count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return count;
}

} // namespace drone_telemetry
