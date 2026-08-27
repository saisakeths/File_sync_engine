#pragma once

#include <cstddef>
#include<string>
using namespace std;

struct Config{
    string srcPath;
    string dstPath;

    static constexpr std::size_t kChunkSize = 4 * 1024 * 1024;
    static constexpr std::size_t kHashWorkerThreads = 0;

    bool parse(int argc, char* argv[]);
};

std::size_t resolveHashWorkerThreads();