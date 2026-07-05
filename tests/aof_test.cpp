#include "aof.h"
#include "store.h"
#include <iostream>
#include <cassert>
#include <cstdio>

int main() {
    const std::string path = "/tmp/test.aof";
    std::remove(path.c_str()); // start clean

    // --- Phase 1: write some commands through the AOF ---
    {
        Aof aof(path, FsyncPolicy::kAlways);
        aof.append({"SET", "foo", "bar"});
        aof.append({"SET", "counter", "1"});
        aof.append({"DEL", "counter"});
        aof.append({"SET", "counter", "2"});
        // Aof destructor flushes on scope exit here.
    }
    std::cout << "PASS: appended 4 commands without throwing\n";

    // --- Phase 2: replay into a fresh Store, verify state matches ---
    Store store;
    Aof::replay(path, [&store](const std::vector<std::string>& args) {
        // Minimal inline dispatcher, just enough to test replay --
        // the real one lives in Server::dispatch.
        if (args[0] == "SET") {
            store.set_string(args[1], args[2]);
        } else if (args[0] == "DEL") {
            store.del(args[1]);
        }
    });

    auto foo = store.get_string("foo");
    assert(foo && *foo == "bar");
    std::cout << "PASS: replayed 'foo' == 'bar'\n";

    auto counter = store.get_string("counter");
    assert(counter && *counter == "2");
    std::cout << "PASS: replayed 'counter' == '2' (SET, DEL, SET replayed in order)\n";

    assert(store.size() == 2);
    std::cout << "PASS: final size is 2 (foo, counter)\n";

    // --- Phase 3: replaying a nonexistent file should be a no-op, not a crash ---
    Store empty_store;
    Aof::replay("/tmp/definitely_does_not_exist.aof", [&empty_store](const std::vector<std::string>& args) {
        empty_store.set_string(args[1], args.size() > 2 ? args[2] : "");
    });
    assert(empty_store.size() == 0);
    std::cout << "PASS: replaying a missing file is a safe no-op\n";

    std::cout << "\nALL AOF TESTS PASSED\n";
    return 0;
}