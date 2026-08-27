#include "config.hpp"

#include "logger.hpp"

#include <algorithm>
#include <iostream>
#include <thread>

using namespace std;

std::size_t resolveHashWorkerThreads() {
    if (Config::kHashWorkerThreads > 0) {
        return Config::kHashWorkerThreads;
    }

    const unsigned hw = std::thread::hardware_concurrency();
    return std::max<std::size_t>(1, hw);
}

bool Config::parse(int argc, char* argv[]){
    auto& logger = fse::Logger::instance();
    logger.debug("Config::parse:Enter argc=<%d>", argc);
    if(argc!=3){
        cout << "usage: ./sync_engine <source path> <destination path>\n";
        return false;
    }

    srcPath = argv[1];
    dstPath = argv[2];
    // logger.info()
    return true;
}