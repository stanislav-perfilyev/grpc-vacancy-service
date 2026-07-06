#!/usr/bin/env python3
"""
Python client for VacancyService — демонстрирует все 4 типа RPC.

Установка:
    pip install grpcio grpcio-tools

Генерация стабов (из корня репо):
    python -m grpc_tools.protoc \
        -I proto \
        --python_out=scripts \
        --grpc_python_out=scripts \
        proto/vacancy.proto
"""

from __future__ import annotations

import sys
import time
from typing import Iterator

import grpc

# Сгенерированные стабы — лежат рядом со скриптом
sys.path.insert(0, str(__file__).rsplit("/", 1)[0])

try:
    import vacancy_pb2          # type: ignore[import]
    import vacancy_pb2_grpc     # type: ignore[import]
except ImportError as exc:
    print(f"[ERROR] Protobuf stubs not found: {exc}")
    print("Run: python -m grpc_tools.protoc -I proto --python_out=scripts "
          "--grpc_python_out=scripts proto/vacancy.proto")
    sys.exit(1)


# ─── helpers ──────────────────────────────────────────────────────────────────

def section(title: str) -> None:
    print(f"\n{'═'*55}")
    print(f"  {title}")
    print(f"{'═'*55}")


def fmt(v: vacancy_pb2.VacancyResponse) -> str:
    sal = ""
    if v.salary_from:
        sal = f"  {v.salary_from}"
        if v.salary_to:
            sal += f"–{v.salary_to}"
        sal += f" {v.currency}"
    return f"[{v.id}] {v.title} @ {v.company} ({v.source}){sal}"


# ─── demo ─────────────────────────────────────────────────────────────────────

def demo(address: str = "localhost:50051") -> None:
    with grpc.insecure_channel(address) as channel:
        stub = vacancy_pb2_grpc.VacancyServiceStub(channel)

        # ── 1. AddVacancy (unary) ──────────────────────────────────────────
        section("1. AddVacancy — unary")
        resp = stub.AddVacancy(vacancy_pb2.VacancyRequest(
            title="Senior Python Developer",
            company="Ozon",
            source="habr",
            salary_from=200_000,
            salary_to=320_000,
            currency="RUB",
            url="https://career.habr.com/vacancies/123",
            tags=["Python", "FastAPI", "PostgreSQL"],
        ))
        print("Added:", fmt(resp))
        added_id = resp.id

        # ── 2. BatchAdd (client stream) ────────────────────────────────────
        section("2. BatchAdd — client streaming")

        def batch_requests() -> Iterator[vacancy_pb2.VacancyRequest]:
            data = [
                ("Go Developer",    "VK",      "hh.kz",  280_000),
                ("Rust Engineer",   "Tinkoff", "habr",   350_000),
                ("Java Backend",    "Sber",    "hh.kz",  300_000),
                ("",                "BadCo",   "tg",           0),  # invalid
            ]
            for title, company, src, sal in data:
                yield vacancy_pb2.VacancyRequest(
                    title=title, company=company, source=src,
                    salary_from=sal, currency="RUB",
                )

        batch_resp = stub.BatchAdd(batch_requests())
        print(f"added={batch_resp.added}  failed={batch_resp.failed}")
        for vid in batch_resp.ids:
            print(f"  id: {vid}")

        # ── 3. GetVacancy (unary) ──────────────────────────────────────────
        section("3. GetVacancy — unary")
        try:
            v = stub.GetVacancy(vacancy_pb2.GetRequest(id=added_id))
            print(fmt(v))
        except grpc.RpcError as e:
            print(f"Error: {e.code()} — {e.details()}")

        # ── 4. SearchVacancies (server stream) ────────────────────────────
        section("4. SearchVacancies — server streaming")
        results = list(stub.SearchVacancies(vacancy_pb2.SearchRequest(
            query="Developer", salary_min=200_000, limit=10,
        )))
        print(f"Found {len(results)} vacancies:")
        for v in results:
            print(" ", fmt(v))

        # ── 5. SyncVacancies (bidirectional stream) ───────────────────────
        section("5. SyncVacancies — bidirectional streaming")

        def sync_requests() -> Iterator[vacancy_pb2.SyncRequest]:
            # Upsert
            yield vacancy_pb2.SyncRequest(
                upsert=vacancy_pb2.VacancyRequest(
                    title="DevOps",
                    company="Cloud",
                    source="tg",
                    salary_from=180_000,
                    currency="RUB",
                )
            )
            time.sleep(0.05)
            # Delete
            yield vacancy_pb2.SyncRequest(delete_id="v-nonexistent-0")

        for sync_resp in stub.SyncVacancies(sync_requests()):
            print(f"  op={sync_resp.op}  id={sync_resp.id}  msg={sync_resp.message}")

        # ── 6. GetStats (unary) ───────────────────────────────────────────
        section("6. GetStats — unary")
        stats = stub.GetStats(vacancy_pb2.StatsRequest())
        print(f"Total: {stats.total}")
        print("By source:", dict(stats.by_source))
        print("By status:", dict(stats.by_status))

    print("\nDemo complete.")


# ─── entry point ──────────────────────────────────────────────────────────────

if __name__ == "__main__":
    addr = sys.argv[1] if len(sys.argv) > 1 else "localhost:50051"
    demo(addr)
