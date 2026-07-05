#include <iostream>
#include <cstdlib>
#include <cstring>

#include "server.h"

int main(int argc, char** argv) {
    int port = 6380; // default; real Redis uses 6379, offset by one to avoid clashing with a real instance
    std::string aof_path = "kvstore.aof";
    FsyncPolicy policy = FsyncPolicy::kEverySecond; // real Redis's actual default

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
        } else {
            port = std::atoi(argv[i]);
        }
    }

    try {
        Server server(port, aof_path, policy);
        server.run();
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }

    return 0;
}