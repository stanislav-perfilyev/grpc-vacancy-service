// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs.io/en/pvs-studio
#pragma once

#include <chrono>
#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include <grpcpp/support/interceptor.h>
#include <grpcpp/support/server_interceptor.h>

namespace grpc_vacancy {

// ─── Per-call interceptor ─────────────────────────────────────────────────────

class LoggingInterceptor : public grpc::experimental::Interceptor {
public:
    explicit LoggingInterceptor(grpc::experimental::ServerRpcInfo* info)
        : method_{info->method()}
        , start_{std::chrono::steady_clock::now()}
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
        }

        methods->Proceed();
    }

private:
    std::string method_;
    std::chrono::steady_clock::time_point start_;
};

// ─── Factory ─────────────────────────────────────────────────────────────────

class LoggingInterceptorFactory
    : public grpc::experimental::ServerInterceptorFactoryInterface {
public:
    grpc::experimental::Interceptor* CreateServerInterceptor(
        grpc::experimental::ServerRpcInfo* info) override {
        return new LoggingInterceptor(info);
    }
};

}  // namespace grpc_vacancy
