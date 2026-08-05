#include "config.hpp"
#include "db/StateDb.hpp"
#include "logger.hpp"
#include "storage/LocalStorage.hpp"
#include "sync/SyncBox.hpp"

#include <filesystem>

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    auto& logger = fse::Logger::instance();

    Config config;

    const bool return_val = config.parse(argc, argv);
    if (!return_val) {
        logger.warning("main: failed to parse source and destination paths");
        return 1;
    }

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

    LocalStorage srcStorage(config.srcPath);
    LocalStorage dstStorage(config.dstPath);

    fse::SyncBox syncBox(stateDb, srcStorage, dstStorage, srcRootId, config.srcPath);
    syncBox.run();

    return 0;
}
