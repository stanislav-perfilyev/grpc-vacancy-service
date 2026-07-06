#pragma once

#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>

#include "storage.h"
#include "vacancy.grpc.pb.h"
#include "vacancy.pb.h"

namespace grpc_vacancy {

class VacancyServiceImpl final : public ::vacancy::VacancyService::Service {
public:
    explicit VacancyServiceImpl(std::shared_ptr<InMemoryStorage> storage)
        : storage_{std::move(storage)}
    {}

    // ── Unary: AddVacancy ────────────────────────────────────────────────────
    grpc::Status AddVacancy(grpc::ServerContext* /*ctx*/,
                            const ::vacancy::VacancyRequest* req,
                            ::vacancy::VacancyResponse* resp) override {
        if (req->title().empty()) {
            return grpc::Status{grpc::StatusCode::INVALID_ARGUMENT,
                                "title must not be empty"};
        }

        const std::string id = storage_->add(*req);
        auto stored = storage_->get(id);
        if (!stored) {
            return grpc::Status{grpc::StatusCode::INTERNAL, "storage write failed"};
        }
        *resp = std::move(*stored);
        return grpc::Status::OK;
    }

    // ── Unary: GetVacancy ────────────────────────────────────────────────────
    grpc::Status GetVacancy(grpc::ServerContext* /*ctx*/,
                            const ::vacancy::GetRequest* req,
                            ::vacancy::VacancyResponse* resp) override {
        if (req->id().empty()) {
            return grpc::Status{grpc::StatusCode::INVALID_ARGUMENT, "id is required"};
        }

        auto result = storage_->get(req->id());
        if (!result) {
            return grpc::Status{grpc::StatusCode::NOT_FOUND,
                                "vacancy not found: " + req->id()};
        }
        *resp = std::move(*result);
        return grpc::Status::OK;
    }

    // ── Unary: GetStats ──────────────────────────────────────────────────────
    grpc::Status GetStats(grpc::ServerContext* /*ctx*/,
                          const ::vacancy::StatsRequest* /*req*/,
                          ::vacancy::StatsResponse* resp) override {
        *resp = storage_->stats();
        return grpc::Status::OK;
    }

    // ── Server streaming: SearchVacancies ───────────────────────────────────
    grpc::Status SearchVacancies(
            grpc::ServerContext* ctx,
            const ::vacancy::SearchRequest* req,
            grpc::ServerWriter<::vacancy::VacancyResponse>* writer) override {

        const auto results = storage_->search(
            req->query(), req->source(), req->salary_min(), req->limit());

        for (const auto& v : results) {
            if (ctx->IsCancelled()) {
                return grpc::Status{grpc::StatusCode::CANCELLED, "client cancelled"};
            }
            if (!writer->Write(v)) break;
        }
        return grpc::Status::OK;
    }

    // ── Client streaming: BatchAdd ───────────────────────────────────────────
    grpc::Status BatchAdd(grpc::ServerContext* /*ctx*/,
                          grpc::ServerReader<::vacancy::VacancyRequest>* reader,
                          ::vacancy::BatchResponse* resp) override {
        ::vacancy::VacancyRequest req;
        int added  = 0;
        int failed = 0;

        while (reader->Read(&req)) {
            if (req.title().empty()) {
                ++failed;
                continue;
            }
            const std::string id = storage_->add(req);
            resp->add_ids(id);
            ++added;
        }

        resp->set_added(added);
        resp->set_failed(failed);
        return grpc::Status::OK;
    }

    // ── Bidirectional streaming: SyncVacancies ───────────────────────────────
    grpc::Status SyncVacancies(
            grpc::ServerContext* ctx,
            grpc::ServerReaderWriter<::vacancy::SyncResponse,
                                    ::vacancy::SyncRequest>* stream) override {
        ::vacancy::SyncRequest  req;
        ::vacancy::SyncResponse resp;

        while (!ctx->IsCancelled() && stream->Read(&req)) {
            resp.Clear();

            switch (req.payload_case()) {
                case ::vacancy::SyncRequest::kUpsert: {
                    const auto& u = req.upsert();
                    if (u.title().empty()) {
                        resp.set_op("error");
                        resp.set_message("title must not be empty");
                    } else {
                        const std::string id = storage_->add(u);
                        resp.set_op("upserted");
                        resp.set_id(id);
                    }
                    break;
                }
                case ::vacancy::SyncRequest::kDeleteId: {
                    const std::string& del_id = req.delete_id();
                    if (storage_->remove(del_id)) {
                        resp.set_op("deleted");
                        resp.set_id(del_id);
                    } else {
                        resp.set_op("error");
                        resp.set_message("not found: " + del_id);
                    }
                    break;
                }
                default:
                    resp.set_op("error");
                    resp.set_message("unknown payload type");
                    break;
            }

            if (!stream->Write(resp)) break;
        }

        return grpc::Status::OK;
    }

private:
    std::shared_ptr<InMemoryStorage> storage_;
};

}  // namespace grpc_vacancy
