#pragma once

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

// Append-Only File persistence, modeled on real Redis AOF.
//
// Every write command gets encoded (as a RESP array, reusing the
// same wire format the network layer already speaks) and appended
// to a log file. On startup, replaying that file from the beginning
// reconstructs the exact sequence of writes, rebuilding state.
//
// Durability tradeoff (this is the important design decision, write
// it in DESIGN.md too):
//
// IMPORTANT nuance verified empirically while building this: a killed
// PROCESS (kill -9) does NOT lose data under any of these policies,
// including kNever. write() hands bytes to the OS page cache
// immediately regardless of fsync; killing our process can't touch
// that, since the data is now the kernel's responsibility. The actual
// risk fsync protects against is power loss or an OS/kernel-level
// crash, where the page cache itself never reaches the physical disk.
// "kill -9 loses recent writes" is a common but imprecise way people
// describe this tradeoff — the precise version is below.
//
//   - kAlways:      fsync() after every single write. Durable against
//                 power loss/OS crash immediately after each write
//                 returns. Slowest, since fsync is a real disk
//                 round-trip, not just a buffered write.
//   - kEverySecond: buffer writes, fsync once per second on a
//                 background thread. Real Redis's actual default.
//                 Durable against process crashes immediately (page
//                 cache). Can lose up to ~1 second of writes
//                 specifically in a power-loss/OS-crash scenario.
//   - kNever:     never call fsync() ourselves; rely entirely on the
//                 OS's own background flush schedule (typically
//                 longer than 1 second) for power-loss durability.
//                 Still safe against plain process crashes, same as
//                 the other policies.
enum class FsyncPolicy {
    kAlways,
    kEverySecond,
    kNever,
};

class Aof {
public:
    // Opens (creating if needed) the AOF file at `path` for
    // appending, with the given durability policy.
    Aof(const std::string& path, FsyncPolicy policy);
    ~Aof();

    // Encodes `args` as a RESP array and appends it to the log.
    // Applies the fsync policy: for kAlways, this call blocks until
    // the write is durable on disk before returning.
    void append(const std::vector<std::string>& args);

    // Replays every command in the AOF file at `path` from the
    // beginning, calling `apply(args)` once per command, in order.
    // Used at startup to rebuild state. Static because it runs
    // before (or independently of) any Aof instance that will later
    // append to the same file.
    static void replay(const std::string& path,
                        const std::function<void(const std::vector<std::string>&)>& apply);

private:
    std::string path_;
    FsyncPolicy policy_;
    int fd_;
    std::mutex write_mutex_;

    // Only used for kEverySecond: a background thread that calls
    // fsync once per second until stop_ is set.
    std::thread fsync_thread_;
    std::atomic<bool> stop_{false};

    void fsync_now();
    void run_periodic_fsync();
};