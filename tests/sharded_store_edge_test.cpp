#include "sharded_store.h"
#include <iostream>
#include <cassert>
#include <stdexcept>

int main() {
    // --- num_shards = 0 should clamp to 1, not crash on modulo-by-zero ---
    ShardedStore zero_shards(0);
    zero_shards.set_string("k", "v");
    assert(zero_shards.get_string("k") == "v");
    assert(zero_shards.num_shards() == 1);
    std::cout << "PASS: num_shards=0 clamps to 1 instead of dividing by zero\n";

    // --- More shards than keys: most shards stay empty, should still work ---
    ShardedStore many_shards(10000);
    many_shards.set_string("only_key", "only_value");
    assert(many_shards.get_string("only_key") == "only_value");
    assert(many_shards.size() == 1);
    std::cout << "PASS: num_shards=10000 with 1 key works correctly (empty shards are fine)\n";

    // --- Exception safety: with_shard_lock must release the lock even
    //     if the callback throws, or every later operation on that key
    //     (from any thread) would deadlock forever. ---
    ShardedStore store(4);
    store.set_string("k", "v");

    bool caught = false;
    try {
        store.with_shard_lock("k", [](Store&) -> std::string {
            throw std::runtime_error("simulated failure inside the callback");
        });
    } catch (const std::runtime_error&) {
        caught = true;
    }
    assert(caught);
    std::cout << "PASS: exception inside with_shard_lock's callback propagates correctly\n";

    // The real test: can we still use that same key's shard afterward?
    // If the lock leaked (wasn't released), this next call would hang
    // forever instead of returning -- so simply reaching the assert
    // below (rather than timing out) IS the test passing.
    store.set_string("k", "v2");
    assert(store.get_string("k") == "v2");
    std::cout << "PASS: shard lock was correctly released after the exception "
                 "(this line proves it -- a leaked lock would have hung here)\n";

    std::cout << "\nALL EDGE CASE TESTS PASSED\n";
    return 0;
}