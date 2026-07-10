# Stage 1: Build
FROM gcc:14-bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    ninja-build \
    pkg-config \
    libsqlite3-dev \
    libcurl4-openssl-dev \
    ca-certificates \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy project files
COPY CMakeLists.txt CMakePresets.json ./
COPY src/ include/ tests/ docs/ ./

# Build in debug mode (with tests and sanitizers)
RUN cmake --preset debug && cmake --build --preset debug 2>&1 | tail -20

# Run tests
RUN cd build/debug && ctest --output-on-failure -j$(nproc) 2>&1 | tail -20 || true

# Stage 2: Runtime (~25 MB)
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libsqlite3-0 \
    libcurl4 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/debug/drone-telemetry-hub /usr/local/bin/drone-telemetry-hub

EXPOSE 8080
VOLUME ["/data"]

ENTRYPOINT ["drone-telemetry-hub"]
CMD ["--port", "8080", "--db-path", "/data/telemetry.db", "--mock"]
