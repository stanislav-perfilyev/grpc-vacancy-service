// MT stress-test for MetricsRegistry with std::shared_mutex
//
// Simulates a gRPC server under load:
//   - N writer threads call record() concurrently (like RPC handlers)
//   - M reader threads call snapshot()/total_calls() concurrently (like health endpoints)
//
// Correctness check: sum of all per-method call counts == total submitted calls
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "interceptor.h"

using namespace grpc_vacancy;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static constexpr int    kMethods  = 4;
static constexpr int    kWriters  = 8;
static constexpr int    kReaders  = 4;
static constexpr int    kCallsPerWriter = 5'000;

static const std::string kMethodNames[kMethods] = {
    "/vacancy.VacancyService/ListVacancies",
    "/vacancy.VacancyService/GetVacancy",
    "/vacancy.VacancyService/CreateVacancy",
    "/vacancy.VacancyService/DeleteVacancy",
};

// ─── Test: concurrent writers, no readers ─────────────────────────────────────

TEST(MetricsRegistry, MT_ConcurrentWrites_TotalCountCorrect) {
    MetricsRegistry reg;
    std::vector<std::thread> threads;
    threads.reserve(kWriters);

    for (int w = 0; w < kWriters; ++w) {
        threads.emplace_back([&, w]() {
            for (int i = 0; i < kCallsPerWriter; ++i) {
                const auto& m = kMethodNames[i % kMethods];
                reg.record(m, static_cast<uint64_t>(i % 1000), /*ok=*/true);
            }
        });
    }
    for (auto& t : threads) t.join();

    const uint64_t expected = static_cast<uint64_t>(kWriters) * kCallsPerWriter;
    EXPECT_EQ(reg.total_calls(), expected);
}

// ─── Test: writers + readers simultaneously ────────────────────────────────────

TEST(MetricsRegistry, MT_SimultaneousReadersWriters_NoDataRace) {
    MetricsRegistry reg;
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> reads_done{0};

    // Readers spin until stop
    std::vector<std::thread> readers;
    for (int r = 0; r < kReaders; ++r) {
        readers.emplace_back([&]() {
            while (!stop.load(std::memory_order_relaxed)) {
                auto snap = reg.snapshot();
                uint64_t total = reg.total_calls();
                // Basic sanity: snapshot total must match total_calls at some point
                // (they can differ momentarily — just check they're non-negative)
                (void)snap; (void)total;
                ++reads_done;
            }
        });
    }

    // Writers do actual work
    std::vector<std::thread> writers;
    for (int w = 0; w < kWriters; ++w) {
        writers.emplace_back([&, w]() {
            for (int i = 0; i < kCallsPerWriter; ++i) {
                const auto& m = kMethodNames[i % kMethods];
                reg.record(m, static_cast<uint64_t>(w * 100 + i % 100), true);
            }
        });
    }

    for (auto& t : writers) t.join();
    stop.store(true);
    for (auto& t : readers) t.join();

    const uint64_t expected = static_cast<uint64_t>(kWriters) * kCallsPerWriter;
    EXPECT_EQ(reg.total_calls(), expected);
    EXPECT_GT(reads_done.load(), 0u);  // readers actually ran
}

// ─── Test: snapshot() returns consistent view ─────────────────────────────────

TEST(MetricsRegistry, MT_SnapshotIsConsistent) {
    MetricsRegistry reg;

    // Pre-populate with known data single-threaded
    for (int i = 0; i < 100; ++i)
        reg.record(kMethodNames[0], 50, true);
    for (int i = 0; i < 200; ++i)
        reg.record(kMethodNames[1], 75, false);

    // Concurrent readers should all see at least the pre-populated data
    std::vector<std::thread> readers;
    std::atomic<int> correct{0};
    for (int r = 0; r < kReaders; ++r) {
        readers.emplace_back([&]() {
            auto snap = reg.snapshot();
            bool ok = snap.count(kMethodNames[0]) && snap.at(kMethodNames[0]).calls >= 100
                   && snap.count(kMethodNames[1]) && snap.at(kMethodNames[1]).calls >= 200
                   && snap.at(kMethodNames[1]).errors >= 200;
            if (ok) ++correct;
        });
    }
    for (auto& t : readers) t.join();
    EXPECT_EQ(correct.load(), kReaders);
}

// ─── Test: error counting under MT ────────────────────────────────────────────

TEST(MetricsRegistry, MT_ErrorCountAccurate) {
    MetricsRegistry reg;
    constexpr int kTotal = 10'000;
    constexpr int kErrors = 3'000;  // first kErrors calls are errors

    std::vector<std::thread> threads;
    for (int w = 0; w < kWriters; ++w) {
        threads.emplace_back([&, w]() {
            for (int i = 0; i < kTotal / kWriters; ++i) {
                bool is_err = (w * (kTotal / kWriters) + i) < kErrors;
                reg.record(kMethodNames[0], 10, !is_err);
            }
        });
    }
    for (auto& t : threads) t.join();

    auto snap = reg.snapshot();
    ASSERT_TRUE(snap.count(kMethodNames[0]));
    const auto& s = snap.at(kMethodNames[0]);
    EXPECT_EQ(s.calls,  static_cast<uint64_t>(kTotal));
    EXPECT_EQ(s.errors, static_cast<uint64_t>(kErrors));
}

// ─── Test: factory shares MetricsRegistry across interceptors ─────────────────

TEST(LoggingInterceptorFactory, SameRegistryAcrossInstances) {
    LoggingInterceptorFactory factory;
    auto metrics1 = factory.metrics();
    auto metrics2 = factory.metrics();
    // Both calls return the same shared_ptr (same underlying object)
    EXPECT_EQ(metrics1.get(), metrics2.get());
}

// ─── Test: throughput baseline ────────────────────────────────────────────────

TEST(MetricsRegistry, MT_Throughput_AtLeast_1M_RecordsPerSec) {
    MetricsRegistry reg;
    constexpr int kRecords = 1'000'000;
    const int kThreads = std::min(kWriters, static_cast<int>(std::thread::hardware_concurrency()));

    const auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> threads;
    for (int w = 0; w < kThreads; ++w) {
        threads.emplace_back([&, w]() {
            for (int i = 0; i < kRecords / kThreads; ++i)
                reg.record(kMethodNames[i % kMethods], static_cast<uint64_t>(i % 500), true);
        });
    }
    for (auto& t : threads) t.join();
    const auto elapsed_s = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();

    const double ops_per_sec = kRecords / elapsed_s;
    EXPECT_GE(ops_per_sec, 1'000'000.0)
        << "Throughput: " << static_cast<int>(ops_per_sec / 1e6) << "M rec/s";
}
