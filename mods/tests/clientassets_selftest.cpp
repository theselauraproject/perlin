#include <cstdlib>
#include <filesystem>
#include <iostream>

#include "client/resources/ClientAssets.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "usage: clientassets_selftest <jar> <data dir>" << std::endl;
        return 2;
    }
    const bool ok = perlin::clientassets::BuildFromJar(argv[1], argv[2]);
    const bool ready = perlin::clientassets::Ready(argv[2]);
    std::cout << "build=" << ok << " ready=" << ready << std::endl;
    return ok && ready ? 0 : 1;
}
