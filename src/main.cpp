#include "config.hpp"
#include "db/StateDb.hpp"
#include "hash/Sha256.hpp"
#include "logger.hpp"
#include "storage/LocalStorage.hpp"
#include "utils/TimeUtils.hpp"

#include <filesystem>

namespace fs = std::filesystem;

namespace {

void scanAndUpsertSourceFiles(fse::StateDb& stateDb, std::int64_t srcRootId,
                              const std::string& srcPath) {
    auto& logger = fse::Logger::instance();
    logger.info("main: scanning source path <%s>", srcPath.c_str());

    LocalStorage storage(srcPath);
    const auto entries = storage.listRecursive();

    std::size_t upserted = 0;
    for (const auto& info : entries) {
        fse::FileRecord record;
        record.rootId = srcRootId;
        record.relPath = info.relPath;
        record.isDirectory = info.isDirectory;
        record.mtime = fse::fileTimeToUnixSeconds(info.lastModified);

        if (info.isDirectory) {
            record.size = std::nullopt;
            record.hash = std::nullopt;
            record.syncStatus = fse::FileSyncStatus::kSkipped;
        } else {
            record.size = static_cast<std::int64_t>(info.size);
            record.syncStatus = fse::FileSyncStatus::kPending;

            const fs::path absPath = fs::path(srcPath) / info.relPath;
            if (const auto hash = fse::hashFile(absPath)) {
                record.hash = *hash;
            } else {
                logger.warning("main: failed to hash <%s>", info.relPath.c_str());
                record.hash = std::nullopt;
            }
        }

        if (stateDb.upsertFile(record) < 0) {
            logger.error("main: upsert failed for <%s>", info.relPath.c_str());
            continue;
        }

        ++upserted;
    }

    logger.info("main: upserted %zu / %zu entries for source root",
                upserted, entries.size());
}

}  // namespace

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

    scanAndUpsertSourceFiles(stateDb, srcRootId, config.srcPath);

    return 0;
}
