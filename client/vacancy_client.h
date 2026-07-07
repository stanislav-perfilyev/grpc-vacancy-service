// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs.io/en/pvs-studio
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "vacancy.grpc.pb.h"
#include "vacancy.pb.h"

namespace grpc_vacancy {

class VacancyClient {
public:
    explicit VacancyClient(std::shared_ptr<grpc::Channel> channel)
        : stub_{::vacancy::VacancyService::NewStub(channel)}
    {}

    // ── Unary: AddVacancy ────────────────────────────────────────────────────
    [[nodiscard]] ::vacancy::VacancyResponse
    AddVacancy(const ::vacancy::VacancyRequest& req) {
        grpc::ClientContext ctx;
        ::vacancy::VacancyResponse resp;
        const auto status = stub_->AddVacancy(&ctx, req, &resp);
        check_status(status, "AddVacancy");
        return resp;
    }

    // ── Unary: GetVacancy ────────────────────────────────────────────────────
    [[nodiscard]] ::vacancy::VacancyResponse
    GetVacancy(const std::string& id) {
        grpc::ClientContext ctx;
        ::vacancy::GetRequest req;
        req.set_id(id);
        ::vacancy::VacancyResponse resp;
        const auto status = stub_->GetVacancy(&ctx, req, &resp);
        check_status(status, "GetVacancy");
        return resp;
    }

    // ── Unary: GetStats ──────────────────────────────────────────────────────
    [[nodiscard]] ::vacancy::StatsResponse GetStats() {
        grpc::ClientContext ctx;
        ::vacancy::StatsRequest req;
        ::vacancy::StatsResponse resp;
        const auto status = stub_->GetStats(&ctx, req, &resp);
        check_status(status, "GetStats");
        return resp;
    }

    // ── Server streaming: SearchVacancies ───────────────────────────────────
    [[nodiscard]] std::vector<::vacancy::VacancyResponse>
    SearchVacancies(const std::string& query,
                    const std::string& source = "",
                    int salary_min = 0,
                    int limit      = 0) {
        grpc::ClientContext ctx;
        ::vacancy::SearchRequest req;
        req.set_query(query);
        req.set_source(source);
        req.set_salary_min(salary_min);
        req.set_limit(limit);

        auto reader = stub_->SearchVacancies(&ctx, req);

        std::vector<::vacancy::VacancyResponse> results;
        ::vacancy::VacancyResponse v;
        while (reader->Read(&v)) {
            results.push_back(v);
        }
        const auto status = reader->Finish();
        check_status(status, "SearchVacancies");
        return results;
    }

    // ── Client streaming: BatchAdd ───────────────────────────────────────────
    [[nodiscard]] ::vacancy::BatchResponse
    BatchAdd(const std::vector<::vacancy::VacancyRequest>& reqs) {
        grpc::ClientContext ctx;
        ::vacancy::BatchResponse resp;
        auto writer = stub_->BatchAdd(&ctx, &resp);

        for (const auto& req : reqs) {
            if (!writer->Write(req)) break;
        }
        writer->WritesDone();
        const auto status = writer->Finish();
        check_status(status, "BatchAdd");
        return resp;
    }

    // ── Bidirectional streaming: SyncVacancies ───────────────────────────────
    void SyncVacancies(
            const std::vector<::vacancy::SyncRequest>& requests,
            std::vector<::vacancy::SyncResponse>& out_responses) {
        grpc::ClientContext ctx;
        auto stream = stub_->SyncVacancies(&ctx);

        // Send all requests
        for (const auto& req : requests) {
            if (!stream->Write(req)) break;
        }
        stream->WritesDone();

        // Receive all responses
        ::vacancy::SyncResponse resp;
        while (stream->Read(&resp)) {
            out_responses.push_back(resp);
        }
        const auto status = stream->Finish();
        check_status(status, "SyncVacancies");
    }

private:
    std::unique_ptr<::vacancy::VacancyService::Stub> stub_;

    static void check_status(const grpc::Status& s, const char* method) {
        if (!s.ok()) {
            throw std::runtime_error{
                std::string{method} + " failed: " + s.error_message()};
        }
    }
};

}  // namespace grpc_vacancy