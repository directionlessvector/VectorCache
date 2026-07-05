#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <variant>
#include <stdexcept>
#include <chrono>

// The closed set of types a value can hold, mirroring Redis's
// String / List / Hash types (Set can follow the same pattern later).
using StringValue = std::string;
using ListValue    = std::vector<std::string>;
using HashValue    = std::unordered_map<std::string, std::string>;

using Value = std::variant<StringValue, ListValue, HashValue>;

// Thrown when a command is used against a key holding the wrong
// type — same idea as Redis's real "WRONGTYPE" error.
class WrongTypeError : public std::runtime_error {
public:
    WrongTypeError() : std::runtime_error("WRONGTYPE key holds the wrong kind of value") {}
};

// TTL return convention, matching real Redis:
//   >= 0  -> seconds remaining
//   -1    -> key exists but has no expiry
//   -2    -> key does not exist
constexpr long long kTtlNoExpiry = -1;
constexpr long long kTtlNoKey = -2;

// A hand-rolled hash table, separate-chaining style, mapping
// std::string -> Value (string, list, or hash). No std::unordered_map
// for the TABLE ITSELF — the point of Day 1 is to own this data
// structure completely, since it'll be the state machine everything
// else builds on. (Using unordered_map INSIDE a HashValue is fine —
// that's just modeling Redis's hash type, a different concern.)
class Store {
public:
    explicit Store(size_t initial_buckets = 16);

    // --- Generic access (used by type-specific helpers below) ---

    // Insert or overwrite a key's value, any type. Clears any
    // existing TTL on the key (matches real Redis SET semantics).
    void set(const std::string& key, Value value);

    // Returns the raw variant if present and not expired,
    // std::nullopt otherwise. Lazily deletes the entry if it has
    // expired (this is where "lazy expiry" actually happens).
    std::optional<Value> get_raw(const std::string& key) const;

    // Returns true if the key existed (and wasn't expired) and was
    // removed.
    bool del(const std::string& key);

    bool exists(const std::string& key) const;

    size_t size() const { return count_; }

    // --- TTL ---

    // Sets the key to expire `seconds` from now. Returns false if
    // the key doesn't exist.
    bool expire(const std::string& key, long long seconds);

    // Sets the key to expire at an absolute unix timestamp (seconds
    // since epoch). This is what the AOF replay path uses: relative
    // EXPIRE commands are translated to absolute EXPIREAT before
    // being written to the log, so replaying the log days later
    // still expires the key at the correct real-world moment instead
    // of granting it a fresh N seconds from replay time.
    bool expire_at(const std::string& key, long long unix_seconds);

    // See kTtlNoExpiry / kTtlNoKey above for the -1/-2 convention.
    long long ttl(const std::string& key) const;

    // Returns the key's absolute expiry as a unix timestamp, or
    // nullopt if the key doesn't exist or has no expiry. Used to
    // translate EXPIRE -> EXPIREAT when writing to the AOF.
    std::optional<long long> expiry_unix_seconds(const std::string& key) const;

    // Removes any TTL on the key, making it persist forever again.
    // Returns true if the key existed and had a TTL to remove.
    bool persist(const std::string& key);

    // Walks every entry and removes ones that have expired. This is
    // "active expiry" — a background thread calls this periodically
    // so that expired-but-never-accessed-again keys don't sit in
    // memory forever waiting for a lazy check that never comes.
    // Returns the number of keys removed.
    size_t sweep_expired();

    // --- Type-specific convenience helpers ---
    // These throw WrongTypeError if the key exists but holds a
    // different type — this is where you'll plug in command
    // handlers later (SET/GET vs LPUSH/LRANGE vs HSET/HGET).

    // Convenience for the common case: SET key "somestring".
    void set_string(const std::string& key, const std::string& value);
    std::optional<std::string> get_string(const std::string& key) const;

    // TODO (later): add list_push/list_range, hash_set/hash_get,
    // following the same WrongTypeError pattern.

private:
    // system_clock (wall-clock), NOT steady_clock: expiry needs to
    // survive across process restarts via the AOF, which means it
    // must be expressible as an absolute unix timestamp. steady_clock
    // has no fixed epoch (it's relative to an arbitrary point, often
    // boot time) so it can't be persisted meaningfully. The tradeoff:
    // system_clock can jump if the OS clock is adjusted (NTP sync,
    // manual change) — steady_clock wouldn't have that problem but
    // can't do this job. This is the standard choice real systems
    // make for the same reason.
    using Clock = std::chrono::system_clock;

    struct Entry {
        std::string key;
        Value value;
        // nullopt = no expiry (persists forever).
        std::optional<Clock::time_point> expires_at;
    };

    std::vector<std::vector<Entry>> buckets_;
    size_t count_;

    static constexpr double kMaxLoadFactor = 0.75;

    size_t bucket_index(const std::string& key) const;
    void maybe_resize();
    void resize(size_t new_bucket_count);

    static bool is_expired(const Entry& entry);

    // Finds the entry for `key` in its bucket. If it exists but has
    // expired, erases it (lazy expiry) and returns nullptr — same as
    // "not found". Non-const overload returns a mutable pointer for
    // in-place updates (used by expire()/persist()).
    Entry* find_live(const std::string& key);
    const Entry* find_live(const std::string& key) const;
};