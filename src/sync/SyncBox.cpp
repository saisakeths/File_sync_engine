#include "sync/SyncBox.hpp"

#include "hash/Sha256.hpp"
#include "logger.hpp"
#include "utils/TimeUtils.hpp"

#include <filesystem>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace fse {

namespace {

bool metadataChanged(const std::optional<FileRecord>& existing,
                     const std::optional<std::int64_t>& mtime,
                     const std::optional<std::int64_t>& size,
                     const std::optional<std::string>& hash) {
    if (!existing.has_value()) {
        return true;
    }

    return existing->mtime != mtime || existing->size != size || existing->hash != hash;
}

}  // namespace

SyncBox::SyncBox(StateDb& db,
                 IStorage& source,
                 IStorage& destination,
                 std::int64_t srcRootId,
                 const std::string& sourceRootPath)
    : db_(db),
      source_(source),
      destination_(destination),
      srcRootId_(srcRootId),
      sourceRootPath_(sourceRootPath) {}

void SyncBox::scan() {
    auto& logger = Logger::instance();
    logger.info("SyncBox::scan: scanning source <%s>", sourceRootPath_.c_str());

    const auto entries = source_.listRecursive();
    std::unordered_set<std::string> seenPaths;
    seenPaths.reserve(entries.size());

    std::size_t upserted = 0;
    std::size_t unchanged = 0;

    for (const auto& info : entries) {
        seenPaths.insert(info.relPath);

        const auto existing = db_.getFile(srcRootId_, info.relPath);

        FileRecord record;
        record.rootId = srcRootId_;
        record.relPath = info.relPath;
        record.isDirectory = info.isDirectory;
        record.mtime = fileTimeToUnixSeconds(info.lastModified);

        if (info.isDirectory) {
            record.size = std::nullopt;
            record.hash = std::nullopt;
            record.syncStatus = FileSyncStatus::kSkipped;
        } else {
            record.size = static_cast<std::int64_t>(info.size);

            const bool mtimeOrSizeChanged =
                !existing.has_value() ||
                existing->mtime != record.mtime ||
                existing->size != record.size;

            if (mtimeOrSizeChanged) {
                const fs::path absPath = fs::path(sourceRootPath_) / info.relPath;
                if (const auto hash = hashFile(absPath)) {
                    record.hash = *hash;
                } else {
                    logger.warning("SyncBox::scan: failed to hash <%s>",
                                   info.relPath.c_str());
                    record.hash = std::nullopt;
                }
            } else {
                record.hash = existing->hash;
            }

            if (metadataChanged(existing, record.mtime, record.size, record.hash)) {
                record.syncStatus = FileSyncStatus::kPending;
            } else {
                record.syncStatus = existing->syncStatus;
                ++unchanged;
            }
        }

        if (db_.upsertFile(record) < 0) {
            logger.error("SyncBox::scan: upsert failed for <%s>", info.relPath.c_str());
            continue;
        }

        ++upserted;
    }

    std::size_t deleted = 0;
    for (const auto& dbFile : db_.listFilesByRoot(srcRootId_)) {
        if (seenPaths.find(dbFile.relPath) != seenPaths.end()) {
            continue;
        }

        if (db_.deleteFile(srcRootId_, dbFile.relPath)) {
            ++deleted;
        } else {
            logger.error("SyncBox::scan: delete failed for <%s>", dbFile.relPath.c_str());
        }
    }

    logger.info(
        "SyncBox::scan: scanned=%zu upserted=%zu unchanged=%zu deleted=%zu",
        entries.size(), upserted, unchanged, deleted);
}

void SyncBox::sync() {
    auto& logger = Logger::instance();
    logger.info("SyncBox::sync: syncing pending files");

    const auto pendingFiles = db_.listFilesByStatus(FileSyncStatus::kPending);

    std::size_t copied = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0;

    for (const auto& file : pendingFiles) {
        if (file.rootId != srcRootId_) {
            continue;
        }

        if (file.isDirectory) {
            if (destination_.createDirs(file.relPath)) {
                db_.updateSyncStatus(srcRootId_, file.relPath, FileSyncStatus::kSkipped);
                ++skipped;
            } else {
                db_.updateSyncStatus(srcRootId_, file.relPath, FileSyncStatus::kFailed);
                logger.error("SyncBox::sync: createDirs failed for <%s>",
                             file.relPath.c_str());
                ++failed;
            }
            continue;
        }

        if (!source_.exist(file.relPath)) {
            db_.updateSyncStatus(srcRootId_, file.relPath, FileSyncStatus::kFailed);
            logger.error("SyncBox::sync: source missing <%s>", file.relPath.c_str());
            ++failed;
            continue;
        }

        const std::vector<std::uint8_t> data = source_.read(file.relPath);
        const bool expectContent = file.size.has_value() && *file.size > 0;
        if (data.empty() && expectContent) {
            db_.updateSyncStatus(srcRootId_, file.relPath, FileSyncStatus::kFailed);
            logger.error("SyncBox::sync: read failed for <%s>", file.relPath.c_str());
            ++failed;
            continue;
        }

        if (!destination_.write(file.relPath, data)) {
            db_.updateSyncStatus(srcRootId_, file.relPath, FileSyncStatus::kFailed);
            logger.error("SyncBox::sync: write failed for <%s>", file.relPath.c_str());
            ++failed;
            continue;
        }

        db_.updateSyncStatus(srcRootId_, file.relPath, FileSyncStatus::kSynced);
        ++copied;
    }

    logger.info("SyncBox::sync: copied=%zu failed=%zu skipped=%zu",
                copied, failed, skipped);
}

void SyncBox::run() {
    scan();
    sync();
}

}  // namespace fse
