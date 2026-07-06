# gRPC C++ Vacancy Service

A production-grade gRPC microservice demonstrating all four RPC communication patterns in C++17.

## Architecture

```
proto/vacancy.proto          ← Protobuf schema + service definition
src/
  storage.h                  ← Thread-safe in-memory store (shared_mutex + atomic)
  interceptor.h              ← gRPC interceptor: per-call logging + latency
  service_impl.h             ← VacancyServiceImpl (all 4 RPC types)
  main.cpp                   ← Server entry point (thread pool + health check)
client/
  vacancy_client.h           ← C++ client wrapper (RAII)
  main.cpp                   ← CLI demo client
tests/
  test_vacancy_service.cpp   ← GTest: service + storage unit tests
scripts/
  client.py                  ← Python client (all RPC types)
```

## RPC Types Demonstrated

| RPC | Method | Pattern |
|-----|--------|---------|
| Unary | `AddVacancy`, `GetVacancy`, `GetStats` | 1 request → 1 response |
| Server streaming | `SearchVacancies` | 1 request → stream of responses |
| Client streaming | `BatchAdd` | stream of requests → 1 response |
| Bidirectional | `SyncVacancies` | stream ↔ stream |

## Key C++ Features

- **`shared_mutex`** — concurrent reads, exclusive writes in `InMemoryStorage`
- **`atomic<uint64_t>`** — lock-free ID counter
- **`[[nodiscard]]`** — enforced on all value-returning methods
- **RAII** — `VacancyClient` wraps stub lifetime; `ServerBuilder` manages server
- **`std::optional`** — `InMemoryStorage::get()` returns `optional<VacancyResponse>`
- **gRPC Interceptor** — `LoggingInterceptor` instruments every RPC call with latency
- **Health check** — `grpc::EnableDefaultHealthCheckService(true)`
- **Server reflection** — `grpc::reflection::InitProtoReflectionServerBuilderPlugin()`

## Build

### Prerequisites (Ubuntu 22.04)

```bash
sudo apt-get install -y \
    build-essential cmake \
    libgrpc++-dev libprotobuf-dev \
    protobuf-compiler protobuf-compiler-grpc \
    libgtest-dev
```

### Compile

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

## Run

```bash
# Start server
./build/grpc_server 0.0.0.0:50051

# In another terminal — C++ demo client
./build/grpc_client localhost:50051
```

### Docker

```bash
docker compose up
```

### Python client

```bash
# Generate stubs first
python -m grpc_tools.protoc \
    -I proto \
    --python_out=scripts \
    --grpc_python_out=scripts \
    proto/vacancy.proto

# Run demo
python scripts/client.py localhost:50051
```

## CI/CD

| Pipeline | Checks |
|----------|--------|
| GitHub Actions | build (gcc-13 + clang-16), ctest, cppcheck, clang-tidy, Docker |
| GitLab CI | build → test → static-analysis → docker (4 stages) |

Static analysis: **cppcheck** (`--enable=all`) + **clang-tidy** (`.clang-tidy` config)

## Inspect running service

```bash
# List available services (requires grpc_cli)
grpc_cli ls localhost:50051

# Call AddVacancy
grpc_cli call localhost:50051 vacancy.VacancyService.AddVacancy \
    "title: 'Senior C++ Dev', company: 'Yandex', source: 'hh.kz', salary_from: 300000, currency: 'RUB'"
```
