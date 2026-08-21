#include "sync/SyncBox.hpp"

#include "config.hpp"
#include "hash/Sha256.hpp"
#include "logger.hpp"
#include "sync/ChunkCopier.hpp"
#include "utils/TimeUtils.hpp"

#include <chrono>
#include <filesystem>
#include <unordered_set>
#include <vector>
#include <algorithm>

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

    auto& logger = Logger::instance();
    logger.error("metadataChanged:: path=<%s>, mtime=<%d>, size=<%d>, hash=<%d>",
                 existing->relPath.c_str(), existing->mtime != mtime,
                 existing->size != size, existing->hash != hash);

    return existing->mtime != mtime || existing->size != size || existing->hash != hash;
}

std::int64_t resolveResumeOffset(IStorage& destination,
                                 const FileRecord& file,
                                 const std::string& tempRelPath) {
    const bool tempExists = destination.exist(tempRelPath);
    std::int64_t tempSize = 0;
    if (tempExists) {
        tempSize = static_cast<std::int64_t>(destination.stat(tempRelPath).size);
    }

    if (file.syncStatus == FileSyncStatus::kSyncing && tempExists &&
        tempSize == file.bytesTransferred) {
        return file.bytesTransferred;
    }

    if (tempExists) {
        destination.remove(tempRelPath);
    }

    return 0;
}

bool markSynced(StateDb& db, std::int64_t rootId, const std::string& relPath) {
    const auto record = db.getFile(rootId, relPath);
    if (!record.has_value()) {
        return false;
    }

    FileRecord updated = *record;
    updated.syncStatus = FileSyncStatus::kSynced;
    updated.bytesTransferred = 0;
    return db.upsertFile(updated) >= 0;
}

bool syncZeroByteFile(IStorage& source,
                      IStorage& destination,
                      StateDb& db,
                      std::int64_t rootId,
                      const FileRecord& file) {
    auto& logger = Logger::instance();
    const std::vector<std::uint8_t> data = source.read(file.relPath);
    if (!data.empty()) {
        db.updateSyncStatus(rootId, file.relPath, FileSyncStatus::kFailed);
        logger.error("SyncBox::sync: zero-byte read returned data for <%s>",
                     file.relPath.c_str());
        return false;
    }

    if (!destination.write(file.relPath, data)) {
        db.updateSyncStatus(rootId, file.relPath, FileSyncStatus::kFailed);
        logger.error("SyncBox::sync: zero-byte write failed for <%s>",
                     file.relPath.c_str());
        return false;
    }

    if (!markSynced(db, rootId, file.relPath)) {
        logger.error("SyncBox::sync: failed to mark synced for <%s>",
                     file.relPath.c_str());
        return false;
    }

    return true;
}

bool syncFileChunked(IStorage& source,
                     IStorage& destination,
                     StateDb& db,
                     const FileRecord& file) {
    auto& logger = Logger::instance();
    const std::string tempRelPath = makeTempRelPath(file.relPath);
    const std::int64_t startOffset =
        resolveResumeOffset(destination, file, tempRelPath);

    if (!db.updateTransferProgress(file.rootId, file.relPath, startOffset)) {
        logger.error("SyncBox::sync: failed to mark syncing for <%s>",
                     file.relPath.c_str());
        return false;
    }

    const ChunkCopyResult copyResult = copyFileChunked(
        source, destination, file, Config::kChunkSize, startOffset, tempRelPath,
        db, 1);

    if (!copyResult.ok) {
        destination.remove(tempRelPath);
        db.updateSyncStatus(file.rootId, file.relPath, FileSyncStatus::kFailed);
        logger.error("SyncBox::sync: chunked copy failed for <%s>: %s",
                     file.relPath.c_str(), copyResult.error.c_str());
        return false;
    }

    if (destination.exist(file.relPath)) {
        if (!destination.remove(file.relPath)) {
            destination.remove(tempRelPath);
            db.updateSyncStatus(file.rootId, file.relPath, FileSyncStatus::kFailed);
            logger.error("SyncBox::sync: failed to remove existing dest for <%s>",
                         file.relPath.c_str());
            return false;
        }
    }

    if (!destination.rename(tempRelPath, file.relPath)) {
        destination.remove(tempRelPath);
        db.updateSyncStatus(file.rootId, file.relPath, FileSyncStatus::kFailed);
        logger.error("SyncBox::sync: atomic rename failed for <%s>",
                     file.relPath.c_str());
        return false;
    }

    if (!markSynced(db, file.rootId, file.relPath)) {
        logger.error("SyncBox::sync: failed to mark synced for <%s>",
                     file.relPath.c_str());
        return false;
    }

    return true;
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
            record.syncStatus = FileSyncStatus::kPending;
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
                record.bytesTransferred = 0;
            } else {
                record.syncStatus = existing->syncStatus;
                record.bytesTransferred = existing->bytesTransferred;
                ++unchanged;
            }
        }

        if (db_.upsertFile(record) < 0) {
            logger.error("SyncBox::scan: upsert failed for <%s>", info.relPath.c_str());
            continue;
        }

        ++upserted;
    }

    std::size_t markedDeleted = 0;
    for (const auto& dbFile : db_.listFilesByRoot(srcRootId_)) {
        if (seenPaths.find(dbFile.relPath) != seenPaths.end()) {
            continue;
        }

        if (dbFile.syncStatus == FileSyncStatus::kDeleted) {
            continue;
        }

        if (db_.updateSyncStatus(srcRootId_, dbFile.relPath, FileSyncStatus::kDeleted)) {
            ++markedDeleted;
        } else {
            logger.error("SyncBox::scan: mark deleted failed for <%s>",
                         dbFile.relPath.c_str());
        }
    }

    logger.info(
        "SyncBox::scan: scanned=%zu upserted=%zu unchanged=%zu marked_deleted=%zu",
        entries.size(), upserted, unchanged, markedDeleted);
}

void SyncBox::sync() {
    auto& logger = Logger::instance();
    logger.info("SyncBox::sync: syncing deleted and pending files");

    std::size_t deleted = 0;
    std::size_t deleteFailed = 0;

    std::vector<FileRecord> deletedFiles =
        db_.listFilesByStatus(FileSyncStatus::kDeleted);
    deletedFiles.erase(
        std::remove_if(deletedFiles.begin(), deletedFiles.end(),
                       [this](const FileRecord& file) {
                           return file.rootId != srcRootId_;
                       }),
        deletedFiles.end());
    std::sort(deletedFiles.begin(), deletedFiles.end(),
              [](const FileRecord& a, const FileRecord& b) {
                  return a.relPath.size() > b.relPath.size();
              });

    for (const auto& file : deletedFiles) {
        destination_.remove(makeTempRelPath(file.relPath));

        if (!destination_.remove(file.relPath)) {
            logger.error("SyncBox::sync: dest remove failed for <%s>",
                         file.relPath.c_str());
            ++deleteFailed;
            continue;
        }

        if (!db_.deleteFile(srcRootId_, file.relPath)) {
            logger.error("SyncBox::sync: delete row failed for <%s>",
                         file.relPath.c_str());
            ++deleteFailed;
            continue;
        }

        ++deleted;
    }

    std::vector<FileRecord> filesToSync =
        db_.listFilesByStatus(FileSyncStatus::kPending);
    const auto syncingFiles = db_.listFilesByStatus(FileSyncStatus::kSyncing);
    filesToSync.insert(filesToSync.end(), syncingFiles.begin(), syncingFiles.end());

    std::size_t copied = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0;

    for (const auto& file : filesToSync) {
        if (file.rootId != srcRootId_) {
            continue;
        }

        if (file.isDirectory) {
            if (destination_.createDirs(file.relPath)) {
                db_.updateSyncStatus(srcRootId_, file.relPath, FileSyncStatus::kSynced);
                ++copied;
            } else {
                db_.updateSyncStatus(srcRootId_, file.relPath, FileSyncStatus::kFailed);
                logger.error("SyncBox::sync: createDirs failed for <%s>",
                             file.relPath.c_str());
                ++failed;
            }
            continue;
        }

        if (!source_.exist(file.relPath)) {
            destination_.remove(makeTempRelPath(file.relPath));
            db_.updateSyncStatus(srcRootId_, file.relPath, FileSyncStatus::kFailed);
            logger.error("SyncBox::sync: source missing <%s>", file.relPath.c_str());
            ++failed;
            continue;
        }

        const bool isZeroByteFile = file.size.has_value() && *file.size == 0;
        const bool synced =
            isZeroByteFile
                ? syncZeroByteFile(source_, destination_, db_, srcRootId_, file)
                : syncFileChunked(source_, destination_, db_, file);

        if (synced) {
            ++copied;
        } else {
            ++failed;
        }
    }

    logger.info("SyncBox::sync: deleted=%zu delete_failed=%zu copied=%zu failed=%zu skipped=%zu",
                deleted, deleteFailed, copied, failed, skipped);
}

void SyncBox::run() {
    using clock = std::chrono::steady_clock;
    const auto runStart = clock::now();

    const auto scanStart = clock::now();
    scan();
    const auto scanMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - scanStart)
            .count();

    const auto syncStart = clock::now();
    sync();
    const auto syncMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - syncStart)
            .count();

    const auto totalMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - runStart)
            .count();

    Logger::instance().info(
        "SyncBox::run: scan_ms=%lld sync_ms=%lld total_ms=%lld",
        static_cast<long long>(scanMs), static_cast<long long>(syncMs),
        static_cast<long long>(totalMs));
}

}  // namespace fse
