#include "config.hpp"
#include "logger.hpp"
#include "storage/IStorage.hpp"
#include "storage/LocalStorage.hpp"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <ctime>

using namespace std;

int main(int argc, char* argv[]) {
    auto& logger = fse::Logger::instance();

    Config config;

    bool return_val = config.parse(argc, argv);
    if(!return_val){
        logger.warning("main: failed to parse source and destination paths");
        return 1;
    }

    // cout << "Source : " << config.srcPath << endl;
    // cout << "Destination : " << config.dstPath << endl;

    logger.info("main: parsing was successful");

    IStorage* src = new LocalStorage(config.srcPath);
    // if(src->exist(config.srcPath)){
    //     cout << "source path is present\n";
        // bool ret_val = src->createDirs("images");
        // if(ret_val){
        // }
        // else{
        //     cout << "failed to create directories\n";
        // }
        auto list = src->listRecursive();
        for(auto info : list){
            // auto info = src->stat("images");
            cout << "path : " << info.relPath << endl;
            auto sysTime = std::chrono::system_clock::now() +
                std::chrono::duration_cast<std::chrono::system_clock::duration>(
                    info.lastModified - decltype(info.lastModified)::clock::now());
            auto ctime = std::chrono::system_clock::to_time_t(sysTime);
            cout << "updated time : " << std::ctime(&ctime);
            cout << "size : " << info.status << endl;
        }
        src->read("hello.txt");
    // else{
    //     cout << "source path is not present\n";
    // }
    // logger.info("File Sync Engine starting");
    // logger.debug("Logger initialized; writing to logs/file_sync_engine.log");
    // logger.warning("Template project - replace this with real sync logic");
    // logger.info("File Sync Engine shutting down");

    return 0;
}
