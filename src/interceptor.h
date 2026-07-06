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
            // Request arrived
            std::cout << "[gRPC] --> " << method_ << '\n';
        }

        if (methods->QueryInterceptionHookPoint(
                grpc::experimental::InterceptionHookPoints::PRE_SEND_STATUS)) {
            // Response dispatched — measure latency
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start_).count();
            std::cout << "[gRPC] <-- " << method_
                      << " (" << elapsed << " µs)\n";
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
