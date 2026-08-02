#include "config.hpp"
#include "logger.hpp"
#include<iostream>

using namespace std;

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