// Multi-threaded RESP2 load-testing client.
//
// Spawns N threads, each opening its own connection and firing a
// fixed number of SET/GET commands, timing each round trip. Reports
// total throughput (ops/sec) and latency percentiles (p50/p99).
//
// Usage: bench <host> <port> <num_threads> <ops_per_thread>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <cerrno>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace {

std::string resp_encode(const std::vector<std::string>& args) {
    std::string out = "*" + std::to_string(args.size()) + "\r\n";
    for (const auto& a : args) {
        out += "$" + std::to_string(a.size()) + "\r\n" + a + "\r\n";
    }
    return out;
}

int connect_to(const std::string& host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        throw std::runtime_error(std::string("connect() failed: ") + strerror(errno));
    }
    return fd;
}

// Sends one command and waits for its reply. Minimal/blocking, good
// enough for a benchmarking client (not trying to pipeline here --
// intentionally measuring one-request-at-a-time latency per thread,
// same as how redis-benchmark's default non-pipelined mode works).
void send_and_recv(int fd, const std::string& encoded) {
    ssize_t sent = send(fd, encoded.data(), encoded.size(), 0);
    if (sent < 0) throw std::runtime_error("send() failed");
    char buf[256];
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0) throw std::runtime_error("recv() failed or connection closed");
}

struct ThreadResult {
    std::vector<double> latencies_us; // one entry per completed op
};

void worker(const std::string& host, int port, int ops_per_thread, int thread_id,
            const std::string& workload, ThreadResult& result) {
    int fd = connect_to(host, port);
    result.latencies_us.reserve(static_cast<size_t>(ops_per_thread));

    std::string key = "benchkey_t" + std::to_string(thread_id);
    std::string set_cmd = resp_encode({"SET", key, "somevalue"});
    std::string get_cmd = resp_encode({"GET", key});

    // Pre-populate the key so get_only mode has something to read.
    send_and_recv(fd, set_cmd);

    for (int i = 0; i < ops_per_thread; ++i) {
        const std::string& cmd =
            (workload == "get_only") ? get_cmd :
            (workload == "set_only") ? set_cmd :
            (i % 2 == 0) ? set_cmd : get_cmd; // mixed (default)

        auto start = std::chrono::steady_clock::now();
        send_and_recv(fd, cmd);
        auto end = std::chrono::steady_clock::now();

        double us = std::chrono::duration<double, std::micro>(end - start).count();
        result.latencies_us.push_back(us);
    }

    close(fd);
}

double percentile(std::vector<double>& sorted_latencies, double p) {
    if (sorted_latencies.empty()) return 0.0;
    size_t idx = static_cast<size_t>(p * static_cast<double>(sorted_latencies.size() - 1));
    return sorted_latencies[idx];
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "usage: " << argv[0] << " <host> <port> <num_threads> <ops_per_thread> [workload: mixed|get_only|set_only]\n";
        return 1;
    }

    std::string host = argv[1];
    int port = std::atoi(argv[2]);
    int num_threads = std::atoi(argv[3]);
    int ops_per_thread = std::atoi(argv[4]);
    std::string workload = (argc >= 6) ? argv[5] : "mixed";

    std::vector<std::thread> threads;
    std::vector<ThreadResult> results(static_cast<size_t>(num_threads));

    auto start = std::chrono::steady_clock::now();

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(worker, host, port, ops_per_thread, t, workload, std::ref(results[static_cast<size_t>(t)]));
    }
    for (auto& th : threads) th.join();

    auto end = std::chrono::steady_clock::now();
    double total_seconds = std::chrono::duration<double>(end - start).count();

    // Merge all threads' latencies into one sorted vector for
    // percentile calculation.
    std::vector<double> all_latencies;
    for (auto& r : results) {
        all_latencies.insert(all_latencies.end(), r.latencies_us.begin(), r.latencies_us.end());
    }
    std::sort(all_latencies.begin(), all_latencies.end());

    long long total_ops = static_cast<long long>(num_threads) * ops_per_thread;
    double throughput = static_cast<double>(total_ops) / total_seconds;

    std::cout << "threads=" << num_threads
              << " ops_per_thread=" << ops_per_thread
              << " total_ops=" << total_ops << "\n";
    std::cout << "total_time_sec=" << total_seconds << "\n";
    std::cout << "throughput_ops_per_sec=" << throughput << "\n";
    std::cout << "p50_latency_us=" << percentile(all_latencies, 0.50) << "\n";
    std::cout << "p99_latency_us=" << percentile(all_latencies, 0.99) << "\n";
    std::cout << "max_latency_us=" << (all_latencies.empty() ? 0.0 : all_latencies.back()) << "\n";

    return 0;
}