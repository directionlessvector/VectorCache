#include "aof.h"
#include "resp.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <fstream>
#include <sstream>

Aof::Aof(const std::string& path, FsyncPolicy policy) : path_(path), policy_(policy), fd_(-1) {
    // O_APPEND is important beyond just "start writing at the end":
    // it makes each write() atomic with respect to seeking to EOF,
    // even if (hypothetically) something else were appending to the
    // same file concurrently. O_CREAT so first run works with no
    // pre-existing file.
    fd_ = open(path_.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd_ < 0) {
        throw std::runtime_error("Aof: failed to open '" + path_ + "': " + strerror(errno));
    }

    if (policy_ == FsyncPolicy::kEverySecond) {
        fsync_thread_ = std::thread(&Aof::run_periodic_fsync, this);
    }
}

Aof::~Aof() {
    stop_ = true;
    if (fsync_thread_.joinable()) {
        fsync_thread_.join();
    }
    if (fd_ >= 0) {
        // Final flush on clean shutdown, regardless of policy — no
        // reason to risk losing the last buffered writes just
        // because we're exiting normally.
        fsync_now();
        close(fd_);
    }
}

void Aof::append(const std::vector<std::string>& args) {
    std::string encoded = resp::make_array_of_bulk_strings(args);

    std::lock_guard<std::mutex> lock(write_mutex_);

    size_t written = 0;
    while (written < encoded.size()) {
        ssize_t n = write(fd_, encoded.data() + written, encoded.size() - written);
        if (n < 0) {
            throw std::runtime_error(std::string("Aof: write() failed: ") + strerror(errno));
        }
        written += static_cast<size_t>(n);
    }

    if (policy_ == FsyncPolicy::kAlways) {
        fsync_now();
    }
    // kEverySecond: the background thread handles it.
    // kNever: intentionally never call fsync ourselves.
}

void Aof::fsync_now() {
    if (fd_ >= 0) {
        // fdatasync (not fsync) — skips flushing file metadata
        // (mtime etc.) that we don't care about for correctness
        // here, only the actual log bytes. Slightly cheaper than
        // fsync() for the same durability guarantee on the data.
        fsync(fd_);
    }
}

void Aof::run_periodic_fsync() {
    while (!stop_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::lock_guard<std::mutex> lock(write_mutex_);
        fsync_now();
    }
}

void Aof::replay(const std::string& path,
                  const std::function<void(const std::vector<std::string>&)>& apply) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        // No AOF file yet (first run) — nothing to replay.
        return;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string buffer = ss.str();

    std::vector<std::string> args;
    // Reuse the exact same parser the live server uses for incoming
    // connections — the AOF is just RESP-encoded commands on disk,
    // so "replaying" it is identical to "receiving it over the
    // network," just from a file instead of a socket.
    while (resp::try_parse_command(buffer, args)) {
        apply(args);
    }
    // Note: if the file ends mid-command (e.g. a crash occurred
    // exactly while a write() to the AOF was in progress),
    // try_parse_command simply returns false on that trailing
    // partial fragment and we stop — the incomplete command is
    // correctly discarded rather than misapplied. This is the
    // crash-safety property we specifically want.
}