#pragma once

#include <string>
#include <mutex>
#include <thread>
#include <atomic>

#include "store.h"
#include "aof.h"

// Thread-per-connection TCP server speaking RESP2. One global mutex
// guards the Store for now (Day 2 goal: correctness). Sharded
// locking to reduce contention is a later, deliberate upgrade — see
// DESIGN.md.
class Server {
public:
    // aof_path: where the append-only log lives. policy: durability
    // tradeoff (see aof.h for the always/every-second/never
    // tradeoffs written out in full).
    Server(int port, const std::string& aof_path, FsyncPolicy policy);
    ~Server();

    // Binds, listens, and loops accepting connections forever
    // (blocking call — spawns a thread per accepted connection).
    void run();

private:
    int port_;
    int listen_fd_;

    Store store_;
    std::mutex store_mutex_;
    Aof aof_;

    std::thread sweep_thread_;
    std::atomic<bool> stop_sweep_{false};

    void handle_client(int client_fd);
    void run_periodic_sweep();

    // Executes one already-parsed command against the store and
    // returns the RESP-encoded reply. Also responsible for logging
    // write commands to the AOF (translating relative TTLs to
    // absolute ones first, per aof.h's replay-correctness note).
    std::string dispatch(const std::vector<std::string>& args);

    // Loads and replays the AOF file at startup, applying each
    // logged command directly to store_ (no AOF re-logging during
    // replay, obviously, or we'd duplicate every entry).
    void load_aof(const std::string& path);
};