// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs.io/en/pvs-studio
#pragma once

#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include <grpcpp/grpcpp.h>
#include <grpcpp/support/interceptor.h>
#include <grpcpp/support/server_interceptor.h>

namespace grpc_vacancy {


// ─── Thread-safe metrics registry (shared across all RPC calls) ───────────────
//
// Read-heavy access pattern → std::shared_mutex:
//   write (record): one call finishes  → unique_lock (exclusive)
//   read  (snapshot): health endpoint  → shared_lock (concurrent)
//
/// MethodStats — per-method RPC telemetry: call count, total latency.
struct MethodStats {
    uint64_t calls{0};
    uint64_t errors{0};
    uint64_t total_us{0};  ///< sum of call durations in µs
};

/// MetricsRegistry — thread-safe registry of per-method gRPC stats.
class MetricsRegistry {
public:
    // Called from interceptor (engine thread) — exclusive write
    void record(const std::string& method, uint64_t elapsed_us, bool ok) noexcept {
        std::unique_lock lock(m_mtx);
        auto& s = m_stats[method];
        ++s.calls;
        s.total_us += elapsed_us;
        if (!ok) ++s.errors;
    }

    // Called from any thread (admin/health) — shared read
    [[nodiscard]] std::unordered_map<std::string, MethodStats> snapshot() const {
        std::shared_lock lock(m_mtx);
        return m_stats;
    }

    [[nodiscard]] uint64_t total_calls() const noexcept {
        std::shared_lock lock(m_mtx);
        uint64_t n = 0;
        for (const auto& [_, s] : m_stats) n += s.calls;
        return n;
    }

private:
    mutable std::shared_mutex                        m_mtx;
    std::unordered_map<std::string, MethodStats>     m_stats;
};

// ─── Per-call interceptor ─────────────────────────────────────────────────────

/// LoggingInterceptor — per-call interceptor: logs method, status, latency.
class LoggingInterceptor : public grpc::experimental::Interceptor {
public:
    LoggingInterceptor(grpc::experimental::ServerRpcInfo* info,
                       std::shared_ptr<MetricsRegistry> metrics)
        : method_{info->method()}
        , start_{std::chrono::steady_clock::now()}
        , m_metrics{std::move(metrics)}
    {}

    void Intercept(grpc::experimental::InterceptorBatchMethods* methods) override {
        if (methods->QueryInterceptionHookPoint(
                grpc::experimental::InterceptionHookPoints::PRE_SEND_INITIAL_METADATA)) {
            // Request arrived — single << prevents interleaving in multi-threaded server
            std::cout << ("[gRPC] --> " + method_ + '\n');
        }

        if (methods->QueryInterceptionHookPoint(
                grpc::experimental::InterceptionHookPoints::PRE_SEND_STATUS)) {
            // Response dispatched — measure latency
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start_).count();
            const std::string log_msg =
                "[gRPC] <-- " + method_ +
                " (" + std::to_string(elapsed) + " µs)\n";
            std::cout << log_msg;
            if (m_metrics)
                m_metrics->record(method_, static_cast<uint64_t>(elapsed), true);
        }

        methods->Proceed();
    }

private:
    std::string method_;
    std::chrono::steady_clock::time_point start_;
    std::shared_ptr<MetricsRegistry> m_metrics;
};

// ─── Factory ─────────────────────────────────────────────────────────────────

/// LoggingInterceptorFactory — creates LoggingInterceptor per-call; gRPC takes ownership.
class LoggingInterceptorFactory
    : public grpc::experimental::ServerInterceptorFactoryInterface {
public:
    LoggingInterceptorFactory()
        : m_metrics{std::make_shared<MetricsRegistry>()}
    {}

    grpc::experimental::Interceptor* CreateServerInterceptor(
        grpc::experimental::ServerRpcInfo* info) override {
        // gRPC interceptor factory API requires raw pointer ownership transfer
        return new LoggingInterceptor(info, m_metrics);  // NOLINT(cppcoreguidelines-owning-memory)
    }

    [[nodiscard]] std::shared_ptr<MetricsRegistry> metrics() const noexcept {
        return m_metrics;
    }

private:
    std::shared_ptr<MetricsRegistry> m_metrics;
};

}  // namespace grpc_vacancy
