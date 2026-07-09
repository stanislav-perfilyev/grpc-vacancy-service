# ─── Stage 1: Builder ─────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    pkg-config \
    libgrpc++-dev \
    libprotobuf-dev \
    protobuf-compiler \
    protobuf-compiler-grpc \
    libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY CMakeLists.txt .
COPY cmake/           cmake/
COPY proto/           proto/
COPY src/             src/
COPY client/          client/
COPY tests/           tests/

RUN cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS="-O2" \
        -DCMAKE_PREFIX_PATH="/usr/lib/x86_64-linux-gnu" \
    && cmake --build build --parallel "$(nproc)"

# ─── Stage 2: Runtime ─────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    # Non-root user
    && groupadd -r appgroup \
    && useradd  -r -g appgroup -s /sbin/nologin appuser

# Copy shared libraries from builder — avoids version-pinned apt package names
# that change between Ubuntu 22.04 updates (e.g. libgrpc++1.46 → libgrpc++1.51 → ...)
COPY --from=builder /usr/lib/x86_64-linux-gnu/libgrpc*.so*    /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libprotobuf*.so* /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libgpr*.so*     /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libupb*.so*     /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libabsl*.so*    /usr/lib/x86_64-linux-gnu/
COPY --from=builder /usr/lib/x86_64-linux-gnu/libre2*.so*     /usr/lib/x86_64-linux-gnu/
RUN ldconfig

COPY --from=builder /build/build/grpc_server /usr/local/bin/grpc_server

USER appuser
EXPOSE 50051

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD grpc_health_probe -addr=:50051 || exit 1

ENTRYPOINT ["/usr/local/bin/grpc_server"]
CMD ["0.0.0.0:50051"]
