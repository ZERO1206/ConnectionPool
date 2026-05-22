#include "ConnectionPool.h"
#include "public.h"
#include <mysql.h>
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <sstream>

using namespace std::chrono;

static std::atomic<long long> total_ops{0};
static std::atomic<long long> success_ops{0};
static std::atomic<long long> fail_ops{0};
static std::atomic<long long> total_latency_ns{0};

void worker(int id, int iterations, const std::string &sql, int progress_interval) {
    auto pool = ConnectionPool::getConnectionPool();
    for (int i = 0; i < iterations; ++i) {
        auto t0 = steady_clock::now();
        std::shared_ptr<Connection> conn = pool->getConnection();
        if (!conn) {
            fail_ops.fetch_add(1, std::memory_order_relaxed);
            total_ops.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        MYSQL_RES *res = conn->query(sql);
        auto t1 = steady_clock::now();
        long long ns = duration_cast<nanoseconds>(t1 - t0).count();
        total_latency_ns.fetch_add(ns, std::memory_order_relaxed);

        if (res) {
            mysql_free_result(res);
            success_ops.fetch_add(1, std::memory_order_relaxed);
        } else {
            fail_ops.fetch_add(1, std::memory_order_relaxed);
        }
        total_ops.fetch_add(1, std::memory_order_relaxed);

        if (progress_interval > 0 && ((i + 1) % progress_interval) == 0) {
            std::ostringstream os;
            os << "Thread[" << id << "] done " << (i + 1) << "/" << iterations;
            LOG(os.str());
        }
    }
}

int main(int argc, char** argv) {
    // Usage: main [threads] [iters_per_thread] [sql]
    int threads = 100;
    int iters = 1000;
    std::string sql = "SELECT 1";

    if (argc >= 2) threads = std::stoi(argv[1]);
    if (argc >= 3) iters = std::stoi(argv[2]);
    if (argc >= 4) sql = argv[3];

    LOG("Stress test starting");
    LOG("Threads: " << threads << ", Iterations/thread: " << iters << ", Query: " << sql);

    auto t_start = steady_clock::now();
    std::vector<std::thread> pool;
    pool.reserve(threads);

    int progress_interval = std::max(1, iters / 10);

    for (int i = 0; i < threads; ++i) {
        pool.emplace_back(worker, i, iters, sql, progress_interval);
    }
    for (auto &th : pool) if (th.joinable()) th.join();
    auto t_end = steady_clock::now();

    long long total = total_ops.load();
    long long succ = success_ops.load();
    long long fail = fail_ops.load();
    long long total_ns = total_latency_ns.load();
    double seconds = duration_cast<duration<double>>(t_end - t_start).count();
    double qps = seconds > 0 ? (double)total / seconds : 0.0;
    double avg_ms = succ > 0 ? (double)total_ns / succ / 1e6 : 0.0;

    std::ostringstream summary;
    summary << "Completed. Total ops=" << total
            << " success=" << succ
            << " fail=" << fail
            << " time(s)=" << seconds
            << " QPS=" << qps
            << " avg_latency_ms=" << avg_ms;
    LOG(summary.str());

    return 0;
}