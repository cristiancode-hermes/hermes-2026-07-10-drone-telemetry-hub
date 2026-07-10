# Drone Telemetry Hub

Arquitectura del sistema de telemetría para drones con REST API y almacenamiento SQLite.

```
┌─────────────────────────────────────────────────────────────┐
│                    Drone Telemetry Hub                       │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────┐    ┌──────────────────┐    ┌──────────────┐   │
│  │  MAVSDK   │    │ TelemetryCollector│    │  REST API    │   │
│  │  o Mock   │───▶│   (Interface)    │───▶│ (cpp-httplib)│   │
│  │  Source   │    │                  │    │ :8080        │   │
│  └──────────┘    └────────┬─────────┘    └──────┬───────┘   │
│                           │                      │           │
│                           ▼                      │           │
│                    ┌──────────────┐               │           │
│                    │   Database   │◄──────────────┘           │
│                    │   (SQLite)   │                           │
│                    └──────────────┘                           │
└─────────────────────────────────────────────────────────────┘
```

## Flujo de datos

1. **Origen de datos**: MAVSDK (drone real) o Mock (simulación local)
2. **Polling loop**: hilo separado que consulta telemetría cada N ms
3. **Persistencia**: cada lectura se almacena en SQLite con WAL mode
4. **API REST**: consulta histórica y última lectura vía HTTP

## API Endpoints

| Método | Ruta                    | Descripción                          |
|--------|-------------------------|--------------------------------------|
| GET    | `/api/health`           | Health check + versión               |
| GET    | `/api/status`           | Estado de conexión + stats           |
| GET    | `/api/telemetry/latest` | Último registro de telemetría        |
| GET    | `/api/telemetry/history`| Histórico paginado (page, per_page)  |
| POST   | `/api/connect`          | Conectar al drone manualmente        |

### Parámetros de /api/telemetry/history

| Parámetro | Tipo   | Default | Descripción                          |
|-----------|--------|---------|--------------------------------------|
| page      | int    | 1       | Número de página (1-indexed)         |
| per_page  | int    | 20      | Items por página (max 1000)          |
| since     | int    | —       | Timestamp Unix mínimo (opcional)     |

## Esquema de base de datos

```sql
CREATE TABLE telemetry (
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
CREATE INDEX idx_telemetry_ts ON telemetry(timestamp DESC);
```

## Colección de telemetría

### MockCollector
- Simula datos GPS del área de Barcelona (41.3874, 2.1686)
- Patrón de vuelo circular/figure-8
- Drenaje gradual de batería (5% por minuto simulado)
- Transiciones de modo: TAKEOFF → HOLD → MISSION → RTL → LAND
- Conexión inmediata, sin hardware requerido

### MavsdkCollector (compilación condicional)
- Requiere `-DDRONE_TELEMETRY_USE_MAVSDK=ON`
- Conexión vía UDP, TCP o serial
- Usa `mavsdk::Telemetry` plugin para datos reales
- Soporta todos los modos de vuelo de PX4/ArduPilot

## Decisiones técnicas

- **C++23**: `std::expected` para manejo de errores sin excepciones, `std::println` con format strings
- **RAII**: Database, MavsdkCollector, TelemetryServer — todo RAII
- **spdlog**: logging estructurado con niveles y rotación
- **WAL mode**: SQLite Write-Ahead Logging para lecturas concurrentes sin bloqueo
- **CORS abierto**: permite consumo desde cualquier frontend web
- **Signal handling**: SIGINT/SIGTERM para graceful shutdown
- **Mock mode por defecto**: desarrollo y CI sin hardware de drone

## Stack tecnológico

| Componente      | Tecnología                 |
|-----------------|----------------------------|
| Lenguaje        | C++23                      |
| HTTP Server     | cpp-httplib v0.18.2        |
| JSON            | nlohmann/json v3.11.3      |
| Logging         | spdlog v1.15.1             |
| Base de datos   | SQLite3                    |
| Drone SDK       | MAVSDK (opcional, v3.x)    |
| Build           | CMake 3.28+, Ninja         |
| Tests           | Catch2 v3.8                |
| Docker          | gcc:14 → debian:bookworm   |
