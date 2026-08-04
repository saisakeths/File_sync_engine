#include "config.hpp"
#include "db/StateDb.hpp"
#include "logger.hpp"
#include "storage/IStorage.hpp"
#include "storage/LocalStorage.hpp"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <ctime>

using namespace std;
namespace fs = std::filesystem;

namespace {

void runFilesTableTests(fse::StateDb& stateDb, std::int64_t srcRootId) {
    auto& logger = fse::Logger::instance();
    logger.info("main: running files table tests");

    fse::FileRecord file;
    file.rootId = srcRootId;
    file.relPath = "test/hello.txt";
    file.isDirectory = false;
    file.mtime = 1'700'000'000;
    file.size = 42;
    file.hash = "abc123";
    file.syncStatus = fse::FileSyncStatus::kPending;

    const std::int64_t fileId = stateDb.upsertFile(file);
    if (fileId < 0) {
        logger.error("main: files test failed at upsertFile (insert)");
        return;
    }
    logger.info("main: upsertFile insert ok id=%lld",
                static_cast<long long>(fileId));

    const auto fetched = stateDb.getFile(srcRootId, file.relPath);
    if (!fetched || fetched->size != 42 || fetched->syncStatus != fse::FileSyncStatus::kPending) {
        logger.error("main: files test failed at getFile");
        return;
    }
    logger.info("main: getFile ok rel_path=<%s> size=%lld",
                fetched->relPath.c_str(),
                static_cast<long long>(*fetched->size));

    file.size = 100;
    file.hash = "def456";
    const std::int64_t updatedId = stateDb.upsertFile(file);
    if (updatedId != fileId) {
        logger.error("main: files test failed at upsertFile (update)");
        return;
    }

    const auto updated = stateDb.getFile(srcRootId, file.relPath);
    if (!updated || updated->size != 100 || updated->hash != "def456") {
        logger.error("main: files test failed at upsertFile metadata update");
        return;
    }
    logger.info("main: upsertFile update ok size=%lld hash=<%s>",
                static_cast<long long>(*updated->size), updated->hash->c_str());

    const auto rootFiles = stateDb.listFilesByRoot(srcRootId);
    if (rootFiles.size() != 1) {
        logger.error("main: files test failed at listFilesByRoot (count=%zu)",
                     rootFiles.size());
        return;
    }
    logger.info("main: listFilesByRoot ok count=%zu", rootFiles.size());

    if (!stateDb.updateSyncStatus(srcRootId, file.relPath, fse::FileSyncStatus::kSynced)) {
        logger.error("main: files test failed at updateSyncStatus");
        return;
    }

    const auto pendingFiles = stateDb.listFilesByStatus(fse::FileSyncStatus::kPending);
    const auto syncedFiles = stateDb.listFilesByStatus(fse::FileSyncStatus::kSynced);
    if (!pendingFiles.empty() || syncedFiles.size() != 1) {
        logger.error("main: files test failed at listFilesByStatus");
        return;
    }
    logger.info("main: updateSyncStatus + listFilesByStatus ok");

    // if (!stateDb.deleteFile(srcRootId, file.relPath)) {
    //     logger.error("main: files test failed at deleteFile");
    //     return;
    // }

    if (stateDb.getFile(srcRootId, file.relPath).has_value()) {
        logger.error("main: files test failed: row still exists after delete");
        return;
    }
    // logger.info("main: deleteFile ok");

    logger.info("main: files table tests passed");
}

}  // namespace

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

    const fs::path dbPath = "state/sync_engine.db";
    if (dbPath.has_parent_path()) {
        fs::create_directories(dbPath.parent_path());
    }

    fse::StateDb stateDb(dbPath.string());
    if (!stateDb.open() || !stateDb.migrate()) {
        logger.error("main: failed to initialize state database");
        return 1;
    }

    const std::int64_t srcRootId = stateDb.upsertRoot(config.srcPath, "source");
    const std::int64_t dstRootId = stateDb.upsertRoot(config.dstPath, "destination");
    if (srcRootId < 0 || dstRootId < 0) {
        logger.error("main: failed to store source/destination roots");
        return 1;
    }

    logger.info("main: stored roots (source id=%lld, destination id=%lld)",
                static_cast<long long>(srcRootId),
                static_cast<long long>(dstRootId));

    runFilesTableTests(stateDb, srcRootId);

    // IStorage* src = new LocalStorage(config.srcPath);
    // // if(src->exist(config.srcPath)){
    // //     cout << "source path is present\n";
    //     // bool ret_val = src->createDirs("images");
    //     // if(ret_val){
    //     // }
    //     // else{
    //     //     cout << "failed to create directories\n";
    //     // }
    //     auto list = src->listRecursive();
    //     for(auto info : list){
    //         // auto info = src->stat("images");
    //         cout << "path : " << info.relPath << endl;
    //         auto sysTime = std::chrono::system_clock::now() +
    //             std::chrono::duration_cast<std::chrono::system_clock::duration>(
    //                 info.lastModified - decltype(info.lastModified)::clock::now());
    //         auto ctime = std::chrono::system_clock::to_time_t(sysTime);
    //         cout << "updated time : " << std::ctime(&ctime);
    //         cout << "size : " << info.status << endl;
    //     }
    //     src->read("hello.txt");
    // else{
    //     cout << "source path is not present\n";
    // }
    // logger.info("File Sync Engine starting");
    // logger.debug("Logger initialized; writing to logs/file_sync_engine.log");
    // logger.warning("Template project - replace this with real sync logic");
    // logger.info("File Sync Engine shutting down");

    return 0;
}
