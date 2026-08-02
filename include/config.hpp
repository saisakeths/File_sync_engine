#pragma once

#include<string>
using namespace std;

struct Config{
    string srcPath;
    string dstPath;

    bool parse(int argc, char* argv[]);
};