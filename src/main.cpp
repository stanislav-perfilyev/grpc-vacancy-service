#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>

#include "interceptor.h"
#include "service_impl.h"
#include "storage.h"

namespace {

std::atomic<bool> g_shutdown{false};

void signal_handler(int /*signum*/) {
    g_shutdown.store(true, std::memory_order_relaxed);
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string address =
        (argc > 1) ? argv[1] : "0.0.0.0:50051";  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

    // ── Built-in health check + server reflection ─────────────────────────
    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    // ── Shared storage ────────────────────────────────────────────────────
    auto storage = std::make_shared<grpc_vacancy::InMemoryStorage>();

    // ── Service impl ──────────────────────────────────────────────────────
    grpc_vacancy::VacancyServiceImpl service{storage};

    // ── Interceptor factories ─────────────────────────────────────────────
    std::vector<std::unique_ptr<grpc::experimental::ServerInterceptorFactoryInterface>> factories;
    factories.emplace_back(
        std::make_unique<grpc_vacancy::LoggingInterceptorFactory>());

    // ── Build server ──────────────────────────────────────────────────────
    grpc::ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    // Thread pool: min(4, hardware_concurrency) server completion threads
    builder.SetSyncServerOption(
        grpc::ServerBuilder::SyncServerOption::NUM_CQS, 1);
    builder.SetSyncServerOption(
        grpc::ServerBuilder::SyncServerOption::MIN_POLLERS, 2);
    builder.SetSyncServerOption(
        grpc::ServerBuilder::SyncServerOption::MAX_POLLERS, 4);

    builder.experimental().SetInterceptorCreators(std::move(factories));

    const auto server = builder.BuildAndStart();
    if (!server) {
        std::cerr << "Failed to start server on " << address << '\n';
        return 1;
    }

    std::cout << "VacancyService listening on " << address << '\n';

    // ── Graceful shutdown on SIGTERM/SIGINT ───────────────────────────────
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT,  signal_handler);

    // Block until signal received
    server->Wait();

    return 0;
}
