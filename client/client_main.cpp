// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs.io/en/pvs-studio
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>

#include "vacancy_client.h"

// ── Demo salary constants ────────────────────────────────────────────────────
static constexpr int kSalarySearchFrom  = 300'000;
static constexpr int kSalarySearchTo    = 450'000;
static constexpr int kSalaryQtDev       = 250'000;
static constexpr int kSalaryEmbedded    = 320'000;
static constexpr int kSalaryBackend     = 280'000;
static constexpr int kSalarySearchMin   = 200'000;
static constexpr int kSalaryUpdateFrom  = 200'000;

namespace {

void print_vacancy(const ::vacancy::VacancyResponse& v) {
    std::cout << "  [" << v.id() << "] "
              << v.title() << " @ " << v.company()
              << "  (" << v.source() << ")";
    if (v.salary_from() > 0) {
        std::cout << "  " << v.salary_from();
        if (v.salary_to() > 0) std::cout << "–" << v.salary_to();
        std::cout << ' ' << v.currency();
    }
    std::cout << '\n';
}

void run_demo(grpc_vacancy::VacancyClient& client) {
    std::cout << "\n══ 1. AddVacancy (unary) ══════════════════════════════\n";
    {
        ::vacancy::VacancyRequest req;
        req.set_title("Senior C++ Developer");
        req.set_company("Yandex");
        req.set_source("hh.kz");
        req.set_salary_from(kSalarySearchFrom);
        req.set_salary_to(kSalarySearchTo);
        req.set_currency("RUB");
        req.set_url("https://hh.kz/vacancy/123");
        req.add_tags("C++17");
        req.add_tags("Qt");
        const auto v = client.AddVacancy(req);
        std::cout << "Added: ";
        print_vacancy(v);
    }

    std::cout << "\n══ 2. BatchAdd (client stream) ════════════════════════\n";
    std::string first_id;
    {
        std::vector<::vacancy::VacancyRequest> batch;

        auto make = [](const std::string& title,
                       const std::string& company,
                       const std::string& src,
                       int sal) {
            ::vacancy::VacancyRequest r;
            r.set_title(title);
            r.set_company(company);
            r.set_source(src);
            r.set_salary_from(sal);
            r.set_currency("RUB");
            return r;
        };

        batch.push_back(make("Qt Developer",         "EPAM",   "habr",  kSalaryQtDev));
        batch.push_back(make("Embedded C++ Engineer","Luxoft",  "hh.kz", kSalaryEmbedded));
        batch.push_back(make("C++ Backend",          "SberTech","habr",  kSalaryBackend));
        batch.push_back(make("",                     "BadCo",   "tg",          0)); // will fail

        const auto resp = client.BatchAdd(batch);
        std::cout << "Batch result: added=" << resp.added()
                  << " failed=" << resp.failed() << '\n';
        for (const auto& id : resp.ids()) {
            std::cout << "  id: " << id << '\n';
        }
        if (resp.ids_size() > 0) first_id = resp.ids(0);
    }

    std::cout << "\n══ 3. GetVacancy (unary) ══════════════════════════════\n";
    if (!first_id.empty()) {
        try {
            const auto v = client.GetVacancy(first_id);
            print_vacancy(v);
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << '\n';
        }
    }

    std::cout << "\n══ 4. SearchVacancies (server stream) ═════════════════\n";
    {
        const auto results = client.SearchVacancies("C++", "", kSalarySearchMin, 10);
        std::cout << "Found " << results.size() << " vacancies:\n";
        for (const auto& v : results) print_vacancy(v);
    }

    std::cout << "\n══ 5. SyncVacancies (bidirectional stream) ════════════\n";
    {
        std::vector<::vacancy::SyncRequest> reqs;

        // Upsert
        {
            ::vacancy::SyncRequest r;
            auto* u = r.mutable_upsert();
            u->set_title("DevOps Engineer");
            u->set_company("MTC");
            u->set_source("tg");
            u->set_salary_from(kSalaryUpdateFrom);
            u->set_currency("RUB");
            reqs.push_back(r);
        }
        // Delete non-existent
        {
            ::vacancy::SyncRequest r;
            r.set_delete_id("v-0000-999");
            reqs.push_back(r);
        }

        std::vector<::vacancy::SyncResponse> resps;
        client.SyncVacancies(reqs, resps);
        for (const auto& r : resps) {
            std::cout << "  op=" << r.op()
                      << " id=" << r.id()
                      << " msg=" << r.message() << '\n';
        }
    }

    std::cout << "\n══ 6. GetStats (unary) ════════════════════════════════\n";
    {
        const auto s = client.GetStats();
        std::cout << "Total: " << s.total() << '\n';
        std::cout << "By source:\n";
        for (const auto& [src, cnt] : s.by_source()) {
            std::cout << "  " << std::setw(10) << src << ": " << cnt << '\n';
        }
        std::cout << "By status:\n";
        for (const auto& [st, cnt] : s.by_status()) {
            std::cout << "  " << std::setw(10) << st << ": " << cnt << '\n';
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string address =
        (argc > 1) ? argv[1] : "localhost:50051";  // NOLINT

    try {
        grpc_vacancy::VacancyClient client{
            grpc::CreateChannel(address, grpc::InsecureChannelCredentials())};

        run_demo(client);
        std::cout << "\nDemo complete.\n";
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}