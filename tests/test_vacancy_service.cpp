#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/health_check_service_interface.h>

#include "../src/service_impl.h"
#include "../src/storage.h"
#include "vacancy.grpc.pb.h"

// ─── Test fixture ─────────────────────────────────────────────────────────────

class VacancyServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        storage_ = std::make_shared<grpc_vacancy::InMemoryStorage>();
        service_ = std::make_unique<grpc_vacancy::VacancyServiceImpl>(storage_);

        grpc::EnableDefaultHealthCheckService(true);
        grpc::reflection::InitProtoReflectionServerBuilderPlugin();

        grpc::ServerBuilder builder;
        builder.AddListeningPort("localhost:0",
                                 grpc::InsecureServerCredentials(),
                                 &port_);
        builder.RegisterService(service_.get());
        server_ = builder.BuildAndStart();

        const std::string addr = "localhost:" + std::to_string(port_);
        stub_ = ::vacancy::VacancyService::NewStub(
            grpc::CreateChannel(addr, grpc::InsecureChannelCredentials()));
    }

    void TearDown() override {
        server_->Shutdown();
    }

    // helper
    [[nodiscard]] ::vacancy::VacancyResponse
    add_one(const std::string& title = "Test Job",
            const std::string& company = "TestCo",
            const std::string& source  = "hh.kz",
            int salary = 100'000) {
        grpc::ClientContext ctx;
        ::vacancy::VacancyRequest req;
        req.set_title(title);
        req.set_company(company);
        req.set_source(source);
        req.set_salary_from(salary);
        req.set_currency("RUB");
        ::vacancy::VacancyResponse resp;
        const auto s = stub_->AddVacancy(&ctx, req, &resp);
        EXPECT_TRUE(s.ok()) << s.error_message();
        return resp;
    }

    std::shared_ptr<grpc_vacancy::InMemoryStorage>        storage_;
    std::unique_ptr<grpc_vacancy::VacancyServiceImpl>     service_;
    std::unique_ptr<grpc::Server>                         server_;
    std::unique_ptr<::vacancy::VacancyService::Stub>      stub_;
    int port_{0};
};

// ─── Unary: AddVacancy ────────────────────────────────────────────────────────

TEST_F(VacancyServiceTest, AddVacancy_Success) {
    const auto v = add_one("Senior C++ Dev", "Yandex", "hh.kz", 300'000);
    EXPECT_FALSE(v.id().empty());
    EXPECT_EQ(v.title(),   "Senior C++ Dev");
    EXPECT_EQ(v.company(), "Yandex");
    EXPECT_EQ(v.source(),  "hh.kz");
    EXPECT_EQ(v.salary_from(), 300'000);
    EXPECT_EQ(v.status(),  "new");
    EXPECT_GT(v.created_at(), 0);
}

TEST_F(VacancyServiceTest, AddVacancy_EmptyTitle_ReturnsInvalidArgument) {
    grpc::ClientContext ctx;
    ::vacancy::VacancyRequest req;
    req.set_company("X");
    ::vacancy::VacancyResponse resp;
    const auto s = stub_->AddVacancy(&ctx, req, &resp);
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

// ─── Unary: GetVacancy ────────────────────────────────────────────────────────

TEST_F(VacancyServiceTest, GetVacancy_Found) {
    const auto added = add_one("Qt Dev", "EPAM", "habr", 250'000);
    grpc::ClientContext ctx;
    ::vacancy::GetRequest req;
    req.set_id(added.id());
    ::vacancy::VacancyResponse resp;
    const auto s = stub_->GetVacancy(&ctx, req, &resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(resp.id(),    added.id());
    EXPECT_EQ(resp.title(), "Qt Dev");
}

TEST_F(VacancyServiceTest, GetVacancy_NotFound) {
    grpc::ClientContext ctx;
    ::vacancy::GetRequest req;
    req.set_id("v-nonexistent-999");
    ::vacancy::VacancyResponse resp;
    const auto s = stub_->GetVacancy(&ctx, req, &resp);
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.error_code(), grpc::StatusCode::NOT_FOUND);
}

TEST_F(VacancyServiceTest, GetVacancy_EmptyId_ReturnsInvalidArgument) {
    grpc::ClientContext ctx;
    ::vacancy::GetRequest req;   // id not set
    ::vacancy::VacancyResponse resp;
    const auto s = stub_->GetVacancy(&ctx, req, &resp);
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.error_code(), grpc::StatusCode::INVALID_ARGUMENT);
}

// ─── Server streaming: SearchVacancies ───────────────────────────────────────

TEST_F(VacancyServiceTest, SearchVacancies_ByQuery) {
    add_one("C++ Engineer", "Sber",    "hh.kz", 280'000);
    add_one("Python Dev",   "Tinkoff", "habr",  200'000);
    add_one("C++ Backend",  "VK",      "hh.kz", 320'000);

    grpc::ClientContext ctx;
    ::vacancy::SearchRequest req;
    req.set_query("C++");
    auto reader = stub_->SearchVacancies(&ctx, req);

    int count = 0;
    ::vacancy::VacancyResponse v;
    while (reader->Read(&v)) {
        EXPECT_NE(v.title().find("C++"), std::string::npos);
        ++count;
    }
    EXPECT_EQ(reader->Finish().error_code(), grpc::StatusCode::OK);
    EXPECT_EQ(count, 2);
}

TEST_F(VacancyServiceTest, SearchVacancies_BySalary) {
    add_one("Junior Dev",  "X", "hh.kz",  80'000);
    add_one("Mid Dev",     "Y", "hh.kz", 200'000);
    add_one("Senior Dev",  "Z", "hh.kz", 350'000);

    grpc::ClientContext ctx;
    ::vacancy::SearchRequest req;
    req.set_salary_min(200'000);
    auto reader = stub_->SearchVacancies(&ctx, req);

    int count = 0;
    ::vacancy::VacancyResponse v;
    while (reader->Read(&v)) {
        EXPECT_GE(v.salary_from(), 200'000);
        ++count;
    }
    EXPECT_EQ(count, 2);
}

TEST_F(VacancyServiceTest, SearchVacancies_BySource) {
    add_one("Dev A", "A", "hh.kz",  200'000);
    add_one("Dev B", "B", "habr",   200'000);
    add_one("Dev C", "C", "hh.kz",  200'000);

    grpc::ClientContext ctx;
    ::vacancy::SearchRequest req;
    req.set_source("habr");
    auto reader = stub_->SearchVacancies(&ctx, req);

    std::vector<::vacancy::VacancyResponse> results;
    ::vacancy::VacancyResponse v;
    while (reader->Read(&v)) results.push_back(v);
    EXPECT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].source(), "habr");
}

// ─── Client streaming: BatchAdd ──────────────────────────────────────────────

TEST_F(VacancyServiceTest, BatchAdd_MixedValid) {
    grpc::ClientContext ctx;
    ::vacancy::BatchResponse resp;
    auto writer = stub_->BatchAdd(&ctx, &resp);

    auto make = [](const std::string& t, const std::string& c) {
        ::vacancy::VacancyRequest r;
        r.set_title(t);
        r.set_company(c);
        r.set_source("tg");
        r.set_currency("RUB");
        return r;
    };

    EXPECT_TRUE(writer->Write(make("Job A", "Co1")));
    EXPECT_TRUE(writer->Write(make("",      "Co2")));  // invalid
    EXPECT_TRUE(writer->Write(make("Job C", "Co3")));
    writer->WritesDone();

    const auto s = writer->Finish();
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(resp.added(),  2);
    EXPECT_EQ(resp.failed(), 1);
    EXPECT_EQ(resp.ids_size(), 2);
}

// ─── Bidirectional streaming: SyncVacancies ──────────────────────────────────

TEST_F(VacancyServiceTest, SyncVacancies_UpsertAndDelete) {
    grpc::ClientContext ctx;
    auto stream = stub_->SyncVacancies(&ctx);

    // Upsert
    {
        ::vacancy::SyncRequest r;
        auto* u = r.mutable_upsert();
        u->set_title("Sync Job");
        u->set_company("SyncCo");
        u->set_source("tg");
        u->set_currency("RUB");
        EXPECT_TRUE(stream->Write(r));
    }
    // Delete non-existent
    {
        ::vacancy::SyncRequest r;
        r.set_delete_id("v-fake-id");
        EXPECT_TRUE(stream->Write(r));
    }
    stream->WritesDone();

    ::vacancy::SyncResponse resp;
    std::vector<std::string> ops;
    while (stream->Read(&resp)) ops.push_back(resp.op());

    EXPECT_EQ(stream->Finish().error_code(), grpc::StatusCode::OK);
    ASSERT_EQ(ops.size(), 2u);
    EXPECT_EQ(ops[0], "upserted");
    EXPECT_EQ(ops[1], "error");
}

// ─── Unary: GetStats ─────────────────────────────────────────────────────────

TEST_F(VacancyServiceTest, GetStats_CountsCorrectly) {
    add_one("Dev1", "A", "hh.kz",  200'000);
    add_one("Dev2", "B", "habr",   200'000);
    add_one("Dev3", "C", "hh.kz",  200'000);

    grpc::ClientContext ctx;
    ::vacancy::StatsRequest req;
    ::vacancy::StatsResponse resp;
    const auto s = stub_->GetStats(&ctx, req, &resp);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(resp.total(), 3);
    EXPECT_EQ(resp.by_source().at("hh.kz"), 2);
    EXPECT_EQ(resp.by_source().at("habr"),  1);
    EXPECT_EQ(resp.by_status().at("new"),   3);
}

// ─── InMemoryStorage unit tests ───────────────────────────────────────────────

TEST(StorageTest, AddGetRemove) {
    grpc_vacancy::InMemoryStorage s;

    ::vacancy::VacancyRequest req;
    req.set_title("Test");
    req.set_company("Co");
    req.set_source("hh.kz");

    const std::string id = s.add(req);
    EXPECT_FALSE(id.empty());
    EXPECT_EQ(s.size(), 1u);

    auto got = s.get(id);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->title(), "Test");

    EXPECT_TRUE(s.remove(id));
    EXPECT_EQ(s.size(), 0u);
    EXPECT_FALSE(s.get(id).has_value());
    EXPECT_FALSE(s.remove(id));  // double remove
}

TEST(StorageTest, SearchCaseInsensitive) {
    grpc_vacancy::InMemoryStorage s;

    ::vacancy::VacancyRequest r1, r2;
    r1.set_title("Senior C++ Developer"); r1.set_company("A"); r1.set_source("x"); r1.set_salary_from(300'000);
    r2.set_title("Python Engineer");      r2.set_company("B"); r2.set_source("x"); r2.set_salary_from(200'000);
    s.add(r1);
    s.add(r2);

    const auto res = s.search("c++", "", 0, 0);
    EXPECT_EQ(res.size(), 1u);
    EXPECT_NE(res[0].title().find("C++"), std::string::npos);
}

TEST(StorageTest, UniqueIds) {
    grpc_vacancy::InMemoryStorage s;
    ::vacancy::VacancyRequest req;
    req.set_title("Job"); req.set_company("X"); req.set_source("y");

    const auto id1 = s.add(req);
    const auto id2 = s.add(req);
    EXPECT_NE(id1, id2);
}
