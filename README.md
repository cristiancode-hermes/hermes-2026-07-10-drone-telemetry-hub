# Drone Telemetry Hub 🚁

**Sistema de telemetría para drones con API REST y almacenamiento SQLite.**

Monitorea la telemetría de un dron en tiempo real vía MAVSDK (PX4/ArduPilot) o mediante datos simulados para desarrollo. Almacena cada lectura en SQLite y las expone a través de una API REST para consumo desde dashboards, frontends web o herramientas CLI.

---

## ✨ Features

- **Telemetría en tiempo real** — posición GPS, altitud, actitud (roll/pitch/yaw), batería y modo de vuelo
- **Mock integrado** — datos simulados realistas sin hardware de drone
- **MAVSDK opcional** — conexión real vía UDP, TCP o serial
- **API REST completa** — health, status, última telemetría, historial paginado
- **SQLite + WAL** — persistencia con lecturas concurrentes sin bloqueo
- **CORS habilitado** — consumo desde cualquier frontend web
- **Graceful shutdown** — captura SIGINT/SIGTERM para cierre ordenado
- **Docker multi-stage** — imagen final de ~25 MB
- **C++23 moderno** — `std::expected`, RAII, concepts, ranges, `std::println`
- **Cobertura de tests** — unitarios, integración y benchmarks con Catch2

---

## 📦 Stack

| Componente      | Tecnología                         |
|-----------------|------------------------------------|
| Lenguaje        | C++23 (GCC 14+)                    |
| HTTP Server     | [cpp-httplib](https://github.com/yhirose/cpp-httplib) v0.18.2 |
| JSON            | [nlohmann/json](https://github.com/nlohmann/json) v3.11.3 |
| Logging         | [spdlog](https://github.com/gabime/spdlog) v1.15.1 |
| Base de datos   | SQLite3 (RAII wrapper)             |
| Drone SDK       | [MAVSDK](https://github.com/mavlink/MAVSDK) v3.x (opcional) |
| Build           | CMake 3.28+, Ninja, v9 presets     |
| Tests           | [Catch2](https://github.com/catchorg/Catch2) v3.8 |
| Contenedor      | Docker multi-stage (gcc:14 → bookworm-slim) |

---

## 🚀 Compilación

### Requisitos

- CMake ≥ 3.28
- Ninja (o Make)
- GCC ≥ 14 o Clang ≥ 18
- SQLite3 development headers (`libsqlite3-dev`)
- Docker (opcional, para build containerizada)

### Local (modo mock — sin drone)

```bash
# Configurar
cmake --preset debug

# Compilar
cmake --build --preset debug

# Ejecutar tests
cd build/debug && ctest --output-on-failure

# Ejecutar servidor
./build/debug/drone-telemetry-hub --port 8080 --db-path /tmp/telemetry.db --mock
```

### Compilación con soporte MAVSDK

```bash
# Requiere MAVSDK instalado (vía vcpkg, Conan o manual)
cmake --preset debug-mavsdk
cmake --build --preset debug-mavsdk

# Ejecutar con conexión real
./build/debug-mavsdk/drone-telemetry-hub --connection udp://:14540 --no-mock
```

### Docker

```bash
docker build -t drone-telemetry-hub .
docker run -p 8080:8080 -v $(pwd)/data:/data drone-telemetry-hub
```

---

## 🔌 API REST

### `GET /api/health`

```json
{
  "status": "ok",
  "version": "1.0.0",
  "uptime": 1741620000,
  "mock_mode": true
}
```

### `GET /api/telemetry/latest`

```json
{
  "id": 42,
  "latitude_deg": 41.3874,
  "longitude_deg": 2.1686,
  "absolute_altitude_m": 62.3,
  "relative_altitude_m": 50.1,
  "roll_deg": 1.2,
  "pitch_deg": -0.8,
  "yaw_deg": 135.0,
  "voltage_v": 15.8,
  "battery_remaining_pct": 92.5,
  "flight_mode": "MISSION",
  "timestamp": 1700000000
}
```

### `GET /api/telemetry/history?page=1&per_page=20&since=1699900000`

```json
{
  "data": [{ ... }, { ... }],
  "page": 1,
  "per_page": 20,
  "total": 150,
  "total_pages": 8
}
```

### `GET /api/status`

```json
{
  "connected": true,
  "telemetry_count": 150,
  "poll_interval_ms": 1000
}
```

### `POST /api/connect`

Inicia la conexión al drone manualmente (sirve para reconectar tras una desconexión).

---

## ⚙️ Configuración

### Variables de entorno

| Variable               | Default           | Descripción                        |
|------------------------|-------------------|------------------------------------|
| `PORT`                 | `8080`            | Puerto del servidor HTTP           |
| `DB_PATH`              | `telemetry.db`    | Ruta de la base de datos SQLite    |
| `DRONE_CONNECTION_URL` | `udp://:14540`    | String de conexión MAVSDK          |
| `POLL_INTERVAL_MS`     | `1000`            | Intervalo de polling (ms)          |
| `MOCK_MODE`            | `true`            | Usar datos simulados               |
| `LOG_LEVEL`            | `info`            | Nivel de log                       |

### Argumentos CLI

```bash
drone-telemetry-hub [--port PORT] [--db-path PATH] [--connection URL]
                    [--poll-ms MS] [--mock|--no-mock] [--log-level LEVEL]
```

---

## 🧪 Tests

```bash
# Con CMake presets
cmake --build --preset debug && cd build/debug && ctest --verbose

# Directo
./build/debug/test_drone_telemetry --success --benchmark-samples 10
```

Los tests incluyen:
- **Unitarios**: parsing de config, serialización JSON
- **Integración**: ciclo de vida del collector, operaciones SQLite
- **Benchmarks**: rendimiento de serialización JSON
- **Edge cases**: base de datos vacía, paths inválidos, paginación

---

## 📁 Estructura del proyecto

```
2026-07-10-drone-telemetry-hub/
├── CMakeLists.txt              # Build principal (C++23, CMake 3.28+)
├── CMakePresets.json           # v9, debug (asan+ubsan), release (LTO)
├── Dockerfile                  # multi-stage gcc:14 → debian slim
├── vcpkg.json                  # manifest mode
├── .gitignore
├── .env.example
├── README.md
├── src/
│   ├── main.cpp                # Entry point + wiring
│   ├── config.cpp              # CLI/env parser
│   ├── database.cpp            # SQLite RAII wrapper
│   ├── telemetry_collector.cpp # Mock + MAVSDK (conditional)
│   └── server.cpp              # REST API server
├── include/
│   └── drone_telemetry/
│       ├── config.hpp
│       ├── database.hpp
│       ├── telemetry_collector.hpp
│       └── server.hpp
├── tests/
│   ├── CMakeLists.txt
│   └── test_main.cpp           # Catch2 tests
└── docs/
    └── architecture.md         # Diagrama de flujo + decisiones técnicas
```

---

## 🐳 Docker

```bash
# Build
docker build -t drone-telemetry-hub .

# Run con mock data
docker run --rm -p 8080:8080 -v $(pwd)/data:/data drone-telemetry-hub

# Run con conexión MAVSDK (build personalizado necesario)
docker run --rm -p 8080:8080 --network host drone-telemetry-hub \
  --connection udp://:14540 --no-mock
```

---

## 🤝 Contribuciones

El proyecto sigue el estándar de código del repositorio:
- C++23 moderno con RAII
- `std::expected` para manejo de errores
- FetchContent para dependencias
- Catch2 v3 para tests
- Git conventional commits

---

## 📄 Licencia

MIT — Cristian (@cristiancode-hermes)
