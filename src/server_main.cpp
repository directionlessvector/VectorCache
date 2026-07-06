#include <iostream>
#include <cstdlib>
#include <cstring>

#include "server.h"

int main(int argc, char** argv) {
    int port = 6380; // default; real Redis uses 6379, offset by one to avoid clashing with a real instance
    std::string aof_path = "kvstore.aof";
    FsyncPolicy policy = FsyncPolicy::kEverySecond; // real Redis's actual default
    size_t num_shards = 32;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--aof" && i + 1 < argc) {
            aof_path = argv[++i];
        } else if (arg == "--fsync" && i + 1 < argc) {
            std::string val = argv[++i];
            if (val == "always") policy = FsyncPolicy::kAlways;
            else if (val == "everysec") policy = FsyncPolicy::kEverySecond;
            else if (val == "never") policy = FsyncPolicy::kNever;
            else {
                std::cerr << "unknown --fsync value '" << val << "' (expected always|everysec|never)\n";
                return 1;
            }
        } else if (arg == "--shards" && i + 1 < argc) {
            num_shards = static_cast<size_t>(std::atoi(argv[++i]));
        } else {
            port = std::atoi(argv[i]);
        }
    }

    try {
        Server server(port, aof_path, policy, num_shards);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }

    return 0;
}