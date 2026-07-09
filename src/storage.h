// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs.io/en/pvs-studio
#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "vacancy.pb.h"

namespace grpc_vacancy {

// ─── Thread-safe in-memory vacancy store ─────────────────────────────────────

/// InMemoryStorage — thread-safe, mutex-guarded vacancy store backed by std::vector.
class InMemoryStorage {
public:
    InMemoryStorage() = default;

    // Not copyable, not movable (mutex is non-copyable)
    InMemoryStorage(const InMemoryStorage&)            = delete;
    InMemoryStorage& operator=(const InMemoryStorage&) = delete;

    [[nodiscard]] std::string add(const ::vacancy::VacancyRequest& req) {
        const std::string id = generate_id();

        ::vacancy::VacancyResponse resp;
        resp.set_id(id);
        resp.set_title(req.title());
        resp.set_company(req.company());
        resp.set_source(req.source());
        resp.set_salary_from(req.salary_from());
        resp.set_salary_to(req.salary_to());
        resp.set_currency(req.currency());
        resp.set_url(req.url());
        resp.set_description(req.description());
        for (const auto& tag : req.tags()) {
            resp.add_tags(tag);
        }
        resp.set_created_at(now_unix());
        resp.set_status("new");

        std::unique_lock lock{mutex_};
        store_[id] = std::move(resp);
        return id;
    }

    [[nodiscard]] std::optional<::vacancy::VacancyResponse> get(const std::string& id) const {
        std::shared_lock lock{mutex_};
        auto it = store_.find(id);
        if (it == store_.end()) return std::nullopt;
        return it->second;
    }

    [[nodiscard]] bool remove(const std::string& id) {
        std::unique_lock lock{mutex_};
        return store_.erase(id) > 0;
    }

    [[nodiscard]] std::vector<::vacancy::VacancyResponse>
    search(const std::string& query, const std::string& source,
           int salary_min, int limit) const {
        std::shared_lock lock{mutex_};

        std::vector<::vacancy::VacancyResponse> results;
        for (const auto& [id, v] : store_) {
            if (!source.empty() && v.source() != source) continue;
            if (salary_min > 0 && v.salary_from() > 0 && v.salary_from() < salary_min) continue;
            if (!query.empty()) {
                const bool title_match   = contains_ci(v.title(),   query);
                const bool company_match = contains_ci(v.company(), query);
                if (!title_match && !company_match) continue;
            }
            results.push_back(v);
            if (limit > 0 && static_cast<int>(results.size()) >= limit) break;
        }
        return results;
    }

    [[nodiscard]] ::vacancy::StatsResponse stats() const {
        std::shared_lock lock{mutex_};
        ::vacancy::StatsResponse s;
        s.set_total(static_cast<int>(store_.size()));
        for (const auto& [id, v] : store_) {
            (*s.mutable_by_source())[v.source()]++;
            (*s.mutable_by_status())[v.status()]++;
        }
        return s;
    }

    [[nodiscard]] std::size_t size() const {
        std::shared_lock lock{mutex_};
        return store_.size();
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ::vacancy::VacancyResponse> store_;
    std::atomic<std::uint64_t> counter_{0};

    [[nodiscard]] std::string generate_id() {
        const auto ts  = now_unix();
        const auto seq = counter_.fetch_add(1, std::memory_order_relaxed);
        return "v-" + std::to_string(ts) + "-" + std::to_string(seq);
    }

    [[nodiscard]] static std::int64_t now_unix() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    [[nodiscard]] static bool contains_ci(const std::string& haystack,
                                          const std::string& needle) noexcept {
        auto it = std::search(
            haystack.begin(), haystack.end(),
            needle.begin(),   needle.end(),
            [](unsigned char a, unsigned char b) {
                return std::tolower(a) == std::tolower(b);
            });
        return it != haystack.end();
    }
};

}  // namespace grpc_vacancy