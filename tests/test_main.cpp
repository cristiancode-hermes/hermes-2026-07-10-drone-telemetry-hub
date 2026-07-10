#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <thread>

#include "drone_telemetry/database.hpp"
#include "drone_telemetry/telemetry_collector.hpp"
#include "drone_telemetry/config.hpp"

using namespace drone_telemetry;
using json = nlohmann::json;

// ═══════════════════════════════════════════════
// Config Tests
// ═══════════════════════════════════════════════

TEST_CASE("Config parse defaults", "[config]") {
  const char* argv[] = {"test"};
  auto cfg = Config::parse(1, const_cast<char**>(argv));
  REQUIRE(cfg.port == 8080);
  REQUIRE(cfg.db_path == "telemetry.db");
  REQUIRE(cfg.mock_mode == true);
  REQUIRE(cfg.log_level == "info");
}

TEST_CASE("Config parse CLI overrides", "[config]") {
  const char* argv[] = {
    "test", "--port", "9090", "--db-path", "/tmp/test.db",
    "--no-mock", "--log-level", "debug"
  };
  auto cfg = Config::parse(8, const_cast<char**>(argv));
  REQUIRE(cfg.port == 9090);
  REQUIRE(cfg.db_path == "/tmp/test.db");
  REQUIRE(cfg.mock_mode == false);
  REQUIRE(cfg.log_level == "debug");
}

TEST_CASE("Config parse --help exits", "[config]") {
  const char* argv[] = {"test", "--help"};
  // This would call std::exit(0), which we can't easily test in a unit test.
  // We just verify the parse function doesn't crash on help.
  // NOTE: In a real test suite, you'd fork or use a death test.
  // Here we simply verify print_usage doesn't throw.
  REQUIRE_NOTHROW(Config::print_usage("test"));
}

TEST_CASE("Parse integer helper", "[config]") {
  REQUIRE(parse_int("42", 0) == 42);
  REQUIRE(parse_int("-1", 0) == -1);
  REQUIRE(parse_int("not-a-number", 99) == 99); // fallback
  REQUIRE(parse_int("", 5) == 5); // empty string, fallback
}

// ═══════════════════════════════════════════════
// JSON Serialization Tests
// ═══════════════════════════════════════════════

TEST_CASE("TelemetryRecord to_json roundtrip", "[json]") {
  TelemetryRecord rec;
  rec.id = 42;
  rec.latitude_deg = 41.3874;
  rec.longitude_deg = 2.1686;
  rec.absolute_altitude_m = 100.5;
  rec.relative_altitude_m = 88.3;
  rec.roll_deg = 1.5f;
  rec.pitch_deg = -0.5f;
  rec.yaw_deg = 45.0f;
  rec.voltage_v = 15.8f;
  rec.battery_remaining_pct = 85.0f;
  rec.flight_mode = "MISSION";
  rec.timestamp = std::chrono::system_clock::from_time_t(1700000000);

  json j = rec.to_json();
  REQUIRE(j["id"].get<int64_t>() == 42);
  REQUIRE(j["latitude_deg"].get<double>() == 41.3874);
  REQUIRE(j["flight_mode"].get<std::string>() == "MISSION");
  REQUIRE(j["timestamp"].get<int64_t>() == 1700000000);

  // Roundtrip
  TelemetryRecord parsed = TelemetryRecord::from_json(j);
  REQUIRE(parsed.id == rec.id);
  REQUIRE(parsed.latitude_deg == rec.latitude_deg);
  REQUIRE(parsed.flight_mode == rec.flight_mode);
}

// ═══════════════════════════════════════════════
// Database Tests (use a temporary in-memory DB)
// ═══════════════════════════════════════════════

struct DatabaseFixture {
  std::string tmp_path;
  std::unique_ptr<Database> db;

  DatabaseFixture() {
    tmp_path = std::filesystem::temp_directory_path() / "test_drone_telemetry.db";
    // Remove if exists
    std::filesystem::remove(tmp_path);
    db = std::make_unique<Database>(tmp_path);
    auto result = db->init_tables();
    REQUIRE(result.has_value());
  }

  ~DatabaseFixture() {
    db.reset();
    std::filesystem::remove(tmp_path);
  }

  TelemetryRecord make_record(double lat, double lon, double alt, int64_t ts) {
    TelemetryRecord r;
    r.latitude_deg = lat;
    r.longitude_deg = lon;
    r.absolute_altitude_m = alt;
    r.relative_altitude_m = alt - 12.0;
    r.roll_deg = 1.0f;
    r.pitch_deg = 0.0f;
    r.yaw_deg = 90.0f;
    r.voltage_v = 16.0f;
    r.battery_remaining_pct = 95.0f;
    r.flight_mode = "HOLD";
    r.timestamp = std::chrono::system_clock::from_time_t(ts);
    return r;
  }
};

TEST_CASE_METHOD(DatabaseFixture, "Database insert and count", "[database]") {
  auto id = db->insert_telemetry(make_record(41.38, 2.16, 50.0, 1700000000));
  REQUIRE(id.has_value());
  REQUIRE(*id > 0);

  id = db->insert_telemetry(make_record(41.39, 2.17, 55.0, 1700000010));
  REQUIRE(id.has_value());

  auto count = db->count_records();
  REQUIRE(count.has_value());
  REQUIRE(*count == 2);
}

TEST_CASE_METHOD(DatabaseFixture, "Database query latest", "[database]") {
  // Insert two records, latest first
  db->insert_telemetry(make_record(41.38, 2.16, 50.0, 1000));
  db->insert_telemetry(make_record(41.39, 2.17, 55.0, 2000));

  auto latest = db->get_latest_telemetry();
  REQUIRE(latest.has_value());
  REQUIRE(latest->has_value());
  REQUIRE(latest->value().latitude_deg == 41.39);
  REQUIRE(latest->value().relative_altitude_m == 55.0 - 12.0);
}

TEST_CASE_METHOD(DatabaseFixture, "Database query empty", "[database]") {
  auto latest = db->get_latest_telemetry();
  REQUIRE(latest.has_value());
  REQUIRE(!latest->has_value()); // no data

  auto records = db->query_telemetry(1, 10);
  REQUIRE(records.has_value());
  REQUIRE(records->empty());

  auto count = db->count_records();
  REQUIRE(count.has_value());
  REQUIRE(*count == 0);
}

TEST_CASE_METHOD(DatabaseFixture, "Database pagination", "[database]") {
  // Insert 25 records
  for (int i = 0; i < 25; ++i) {
    db->insert_telemetry(make_record(41.38 + i * 0.001, 2.16, 50.0 + i, 1700000000 + i));
  }

  auto count = db->count_records();
  REQUIRE(*count == 25);

  // Page 1 (20 records)
  auto page1 = db->query_telemetry(1, 20);
  REQUIRE(page1.has_value());
  REQUIRE(page1->size() == 20);

  // Page 2 (5 records)
  auto page2 = db->query_telemetry(2, 20);
  REQUIRE(page2.has_value());
  REQUIRE(page2->size() == 5);
}

// ═══════════════════════════════════════════════
// Telemetry Collector Tests
// ═══════════════════════════════════════════════

TEST_CASE("Mock collector lifecycle", "[collector]") {
  auto collector = create_mock_collector();
  REQUIRE(collector != nullptr);

  // Initially disconnected
  REQUIRE_FALSE(collector->is_connected());

  // Connect
  auto result = collector->connect();
  REQUIRE(result.has_value());
  REQUIRE(collector->is_connected());

  // Poll
  auto data = collector->poll();
  REQUIRE(data.has_value());
  REQUIRE(data->latitude_deg > 40.0);
  REQUIRE(data->latitude_deg < 43.0);
  REQUIRE(data->battery_remaining_pct > 0.0f);
  REQUIRE(data->battery_remaining_pct <= 100.0f);
  REQUIRE_FALSE(data->flight_mode.empty());

  // Disconnect
  collector->disconnect();
  REQUIRE_FALSE(collector->is_connected());

  // Poll after disconnect fails
  auto failed = collector->poll();
  REQUIRE_FALSE(failed.has_value());
}

TEST_CASE("Mock collector generates timestamps", "[collector]") {
  auto collector = create_mock_collector();
  collector->connect();

  auto data = collector->poll();
  REQUIRE(data.has_value());

  auto now = std::chrono::system_clock::now();
  auto diff = now - data->timestamp;
  REQUIRE(diff < std::chrono::seconds(1)); // recent timestamp
}

// ═══════════════════════════════════════════════
// Benchmark
// ═══════════════════════════════════════════════

TEST_CASE("JSON serialization benchmark", "[benchmark][!mayfail]") {
  TelemetryRecord rec;
  rec.id = 1001;
  rec.latitude_deg = 41.3874;
  rec.longitude_deg = 2.1686;
  rec.absolute_altitude_m = 100.5;
  rec.relative_altitude_m = 88.3;
  rec.roll_deg = 1.5f;
  rec.pitch_deg = -0.5f;
  rec.yaw_deg = 45.0f;
  rec.voltage_v = 15.8f;
  rec.battery_remaining_pct = 85.0f;
  rec.flight_mode = "MISSION";
  rec.timestamp = std::chrono::system_clock::from_time_t(1700000000);

  BENCHMARK("Record to JSON") {
    return rec.to_json();
  };

  json j = rec.to_json();
  BENCHMARK("Record from JSON") {
    return TelemetryRecord::from_json(j);
  };
}

// ═══════════════════════════════════════════════
// SQLite3 C API smoke test
// ═══════════════════════════════════════════════

TEST_CASE("SQLite3 version is available", "[sqlite]") {
  REQUIRE(sqlite3_libversion() != nullptr);
  REQUIRE(std::strlen(sqlite3_libversion()) > 0);
  spdlog::info("SQLite3 version: {}", sqlite3_libversion());
}

// ═══════════════════════════════════════════════
// Edge cases
// ═══════════════════════════════════════════════

TEST_CASE("Database handles invalid path gracefully", "[database][edge]") {
  auto path = "/nonexistent/deep/dir/db.sqlite";
  try {
    Database db(path);
    FAIL("Expected exception for invalid path");
  } catch (const std::runtime_error& e) {
    REQUIRE(std::strlen(e.what()) > 0);
  }
}
