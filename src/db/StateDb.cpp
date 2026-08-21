#include "db/StateDb.hpp"

#include "logger.hpp"

#include <chrono>
#include <sqlite3.h>

namespace fse {

namespace {

constexpr const char* kCreateRootsTableSql = R"(
CREATE TABLE IF NOT EXISTS roots (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    path        TEXT NOT NULL UNIQUE,
    role        TEXT NOT NULL CHECK(role IN ('source', 'destination')),
    created_at  INTEGER NOT NULL,
    updated_at  INTEGER NOT NULL
);
)";

constexpr const char* kCreateFilesTableSql = R"(
CREATE TABLE IF NOT EXISTS files (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    root_id             INTEGER NOT NULL,
    rel_path            TEXT NOT NULL,
    is_directory        INTEGER NOT NULL DEFAULT 0 CHECK(is_directory IN (0, 1)),
    mtime               INTEGER,
    size                INTEGER,
    hash                TEXT,
    sync_status         TEXT NOT NULL DEFAULT 'pending'
                        CHECK(sync_status IN ('pending', 'syncing', 'synced', 'failed', 'skipped', 'deleted')),
    bytes_transferred   INTEGER NOT NULL DEFAULT 0,
    created_at          INTEGER NOT NULL,
    updated_at          INTEGER NOT NULL,
    UNIQUE(root_id, rel_path),
    FOREIGN KEY(root_id) REFERENCES roots(id) ON DELETE CASCADE
);
)";

constexpr const char* kCreateFilesIndexesSql = R"(
CREATE INDEX IF NOT EXISTS idx_files_root_id ON files(root_id);
CREATE INDEX IF NOT EXISTS idx_files_sync_status ON files(sync_status);
CREATE INDEX IF NOT EXISTS idx_files_root_status ON files(root_id, sync_status);
)";

bool execSql(sqlite3* db, const char* sql, const char* context) {
    auto& logger = Logger::instance();
    char* errorMessage = nullptr;
    const int result = sqlite3_exec(db, sql, nullptr, nullptr, &errorMessage);

    if (result != SQLITE_OK) {
        logger.error("StateDb::%s: %s", context,
                     errorMessage != nullptr ? errorMessage : "unknown error");
        sqlite3_free(errorMessage);
        return false;
    }

    return true;
}

bool columnExists(sqlite3* db, const char* table, const char* column) {
    const std::string pragmaSql =
        std::string("PRAGMA table_info(") + table + ");";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, pragmaSql.c_str(), -1, &statement, nullptr) !=
        SQLITE_OK) {
        return false;
    }

    bool found = false;
    while (sqlite3_step(statement) == SQLITE_ROW) {
        const char* name =
            reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        if (name != nullptr && column == std::string(name)) {
            found = true;
            break;
        }
    }

    sqlite3_finalize(statement);
    return found;
}

bool migrateFilesTableV2(sqlite3* db) {
    auto& logger = Logger::instance();

    if (columnExists(db, "files", "bytes_transferred")) {
        return true;
    }

    logger.info("StateDb::migrate: upgrading files table to v2");

    constexpr const char* kCreateFilesTableV2Sql = R"(
CREATE TABLE files_v2 (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    root_id             INTEGER NOT NULL,
    rel_path            TEXT NOT NULL,
    is_directory        INTEGER NOT NULL DEFAULT 0 CHECK(is_directory IN (0, 1)),
    mtime               INTEGER,
    size                INTEGER,
    hash                TEXT,
    sync_status         TEXT NOT NULL DEFAULT 'pending'
                        CHECK(sync_status IN ('pending', 'syncing', 'synced', 'failed', 'skipped', 'deleted')),
    bytes_transferred   INTEGER NOT NULL DEFAULT 0,
    created_at          INTEGER NOT NULL,
    updated_at          INTEGER NOT NULL,
    UNIQUE(root_id, rel_path),
    FOREIGN KEY(root_id) REFERENCES roots(id) ON DELETE CASCADE
);
)";

    constexpr const char* kCopyFilesV2Sql = R"(
INSERT INTO files_v2 (
    id, root_id, rel_path, is_directory, mtime, size, hash,
    sync_status, bytes_transferred, created_at, updated_at
)
SELECT id, root_id, rel_path, is_directory, mtime, size, hash,
       sync_status, 0, created_at, updated_at
FROM files;
)";

    if (!execSql(db, kCreateFilesTableV2Sql, "migrateFilesTableV2:create")) {
        return false;
    }
    if (!execSql(db, kCopyFilesV2Sql, "migrateFilesTableV2:copy")) {
        return false;
    }
    if (!execSql(db, "DROP TABLE files;", "migrateFilesTableV2:drop")) {
        return false;
    }
    if (!execSql(db, "ALTER TABLE files_v2 RENAME TO files;",
                 "migrateFilesTableV2:rename")) {
        return false;
    }
    if (!execSql(db, kCreateFilesIndexesSql, "migrateFilesTableV2:indexes")) {
        return false;
    }

    logger.info("StateDb::migrate: files table upgraded to v2");
    return true;
}

std::int64_t currentUnixTime() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void bindOptionalInt64(sqlite3_stmt* statement, int index,
                       const std::optional<std::int64_t>& value) {
    if (value.has_value()) {
        sqlite3_bind_int64(statement, index, *value);
    } else {
        sqlite3_bind_null(statement, index);
    }
}

void bindOptionalText(sqlite3_stmt* statement, int index,
                      const std::optional<std::string>& value) {
    if (value.has_value()) {
        sqlite3_bind_text(statement, index, value->c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(statement, index);
    }
}

std::optional<std::int64_t> readOptionalInt64(sqlite3_stmt* statement, int index) {
    if (sqlite3_column_type(statement, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_int64(statement, index);
}

std::optional<std::string> readOptionalText(sqlite3_stmt* statement, int index) {
    if (sqlite3_column_type(statement, index) == SQLITE_NULL) {
        return std::nullopt;
    }
    return reinterpret_cast<const char*>(sqlite3_column_text(statement, index));
}

FileRecord rowToFileRecord(sqlite3_stmt* statement) {
    FileRecord record;
    record.id = sqlite3_column_int64(statement, 0);
    record.rootId = sqlite3_column_int64(statement, 1);
    record.relPath = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
    record.isDirectory = sqlite3_column_int(statement, 3) != 0;
    record.mtime = readOptionalInt64(statement, 4);
    record.size = readOptionalInt64(statement, 5);
    record.hash = readOptionalText(statement, 6);
    record.syncStatus = reinterpret_cast<const char*>(sqlite3_column_text(statement, 7));
    record.bytesTransferred = sqlite3_column_int64(statement, 8);
    record.createdAt = sqlite3_column_int64(statement, 9);
    record.updatedAt = sqlite3_column_int64(statement, 10);
    return record;
}

constexpr const char* kSelectFileColumns = R"(
SELECT id, root_id, rel_path, is_directory, mtime, size, hash,
       sync_status, bytes_transferred, created_at, updated_at
)";

}  // namespace

StateDb::StateDb(std::string dbPath) : db_(nullptr), dbPath_(std::move(dbPath)) {}

StateDb::~StateDb() {
    close();
}

bool StateDb::isOpen() const {
    return db_ != nullptr;
}

bool StateDb::open() {
    if (db_ != nullptr) {
        return true;
    }

    auto& logger = Logger::instance();
    if (sqlite3_open(dbPath_.c_str(), &db_) != SQLITE_OK) {
        logger.error("StateDb::open: failed to open database <%s>: %s",
                     dbPath_.c_str(), sqlite3_errmsg(db_));
        db_ = nullptr;
        return false;
    }

    logger.info("StateDb::open: opened database <%s>", dbPath_.c_str());
    return true;
}

void StateDb::close() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool StateDb::migrate() {
    if (db_ == nullptr) {
        return false;
    }

    auto& logger = Logger::instance();

    if (!execSql(db_, kCreateRootsTableSql, "migrate")) {
        return false;
    }
    logger.info("StateDb::migrate: roots table ready");

    if (!execSql(db_, kCreateFilesTableSql, "migrate")) {
        return false;
    }
    logger.info("StateDb::migrate: files table ready");

    if (!migrateFilesTableV2(db_)) {
        return false;
    }

    if (!execSql(db_, kCreateFilesIndexesSql, "migrate")) {
        return false;
    }
    logger.info("StateDb::migrate: files indexes ready");

    return true;
}

std::int64_t StateDb::upsertRoot(const std::string& path, const std::string& role) {
    if (db_ == nullptr) {
        return -1;
    }

    auto& logger = Logger::instance();
    const std::int64_t now = currentUnixTime();

    const char* upsertSql = R"(
INSERT INTO roots (path, role, created_at, updated_at)
VALUES (?, ?, ?, ?)
ON CONFLICT(path) DO UPDATE SET
    role = excluded.role,
    updated_at = excluded.updated_at;
)";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, upsertSql, -1, &statement, nullptr) != SQLITE_OK) {
        logger.error("StateDb::upsertRoot: prepare failed: %s", sqlite3_errmsg(db_));
        return -1;
    }

    sqlite3_bind_text(statement, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(statement, 2, role.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 3, now);
    sqlite3_bind_int64(statement, 4, now);

    if (sqlite3_step(statement) != SQLITE_DONE) {
        logger.error("StateDb::upsertRoot: step failed: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(statement);
        return -1;
    }

    sqlite3_finalize(statement);

    const char* selectSql = "SELECT id FROM roots WHERE path = ?;";
    if (sqlite3_prepare_v2(db_, selectSql, -1, &statement, nullptr) != SQLITE_OK) {
        logger.error("StateDb::upsertRoot: select prepare failed: %s",
                     sqlite3_errmsg(db_));
        return -1;
    }

    sqlite3_bind_text(statement, 1, path.c_str(), -1, SQLITE_TRANSIENT);

    std::int64_t rootId = -1;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        rootId = sqlite3_column_int64(statement, 0);
    } else {
        logger.error("StateDb::upsertRoot: root not found after upsert <%s>",
                     path.c_str());
    }

    sqlite3_finalize(statement);
    logger.info("StateDb::upsertRoot: path=<%s> role=<%s> id=<%lld>",
                path.c_str(), role.c_str(), static_cast<long long>(rootId));
    return rootId;
}

std::optional<RootRecord> StateDb::getRootByPath(const std::string& path) {
    if (db_ == nullptr) {
        return std::nullopt;
    }

    const char* selectSql = R"(
SELECT id, path, role, created_at, updated_at
FROM roots
WHERE path = ?;
)";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, selectSql, -1, &statement, nullptr) != SQLITE_OK) {
        auto& logger = Logger::instance();
        logger.error("StateDb::getRootByPath: prepare failed: %s",
                     sqlite3_errmsg(db_));
        return std::nullopt;
    }

    sqlite3_bind_text(statement, 1, path.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<RootRecord> record;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        RootRecord root;
        root.id = sqlite3_column_int64(statement, 0);
        root.path = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        root.role = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
        root.createdAt = sqlite3_column_int64(statement, 3);
        root.updatedAt = sqlite3_column_int64(statement, 4);
        record = std::move(root);
    }

    sqlite3_finalize(statement);
    return record;
}

std::int64_t StateDb::upsertFile(const FileRecord& file) {
    if (db_ == nullptr) {
        return -1;
    }

    auto& logger = Logger::instance();
    const std::int64_t now = currentUnixTime();
    const std::int64_t createdAt = file.createdAt > 0 ? file.createdAt : now;
    const std::int64_t updatedAt = now;

    const char* upsertSql = R"(
INSERT INTO files (
    root_id, rel_path, is_directory, mtime, size, hash,
    sync_status, bytes_transferred, created_at, updated_at
) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
ON CONFLICT(root_id, rel_path) DO UPDATE SET
    is_directory      = excluded.is_directory,
    mtime             = excluded.mtime,
    size              = excluded.size,
    hash              = excluded.hash,
    sync_status       = excluded.sync_status,
    bytes_transferred = CASE
        WHEN excluded.sync_status = 'pending' THEN 0
        ELSE excluded.bytes_transferred
    END,
    updated_at        = excluded.updated_at;
)";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, upsertSql, -1, &statement, nullptr) != SQLITE_OK) {
        logger.error("StateDb::upsertFile: prepare failed: %s", sqlite3_errmsg(db_));
        return -1;
    }

    sqlite3_bind_int64(statement, 1, file.rootId);
    sqlite3_bind_text(statement, 2, file.relPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(statement, 3, file.isDirectory ? 1 : 0);
    bindOptionalInt64(statement, 4, file.mtime);
    bindOptionalInt64(statement, 5, file.size);
    bindOptionalText(statement, 6, file.hash);
    sqlite3_bind_text(statement, 7, file.syncStatus.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 8, file.bytesTransferred);
    sqlite3_bind_int64(statement, 9, createdAt);
    sqlite3_bind_int64(statement, 10, updatedAt);

    if (sqlite3_step(statement) != SQLITE_DONE) {
        logger.error("StateDb::upsertFile: step failed: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(statement);
        return -1;
    }

    sqlite3_finalize(statement);

    const char* selectSql = "SELECT id FROM files WHERE root_id = ? AND rel_path = ?;";
    if (sqlite3_prepare_v2(db_, selectSql, -1, &statement, nullptr) != SQLITE_OK) {
        logger.error("StateDb::upsertFile: select prepare failed: %s",
                     sqlite3_errmsg(db_));
        return -1;
    }

    sqlite3_bind_int64(statement, 1, file.rootId);
    sqlite3_bind_text(statement, 2, file.relPath.c_str(), -1, SQLITE_TRANSIENT);

    std::int64_t fileId = -1;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        fileId = sqlite3_column_int64(statement, 0);
    } else {
        logger.error("StateDb::upsertFile: file not found after upsert root_id=<%lld> rel_path=<%s>",
                     static_cast<long long>(file.rootId), file.relPath.c_str());
    }

    sqlite3_finalize(statement);
    return fileId;
}

std::optional<FileRecord> StateDb::getFile(std::int64_t rootId,
                                           const std::string& relPath) {
    if (db_ == nullptr) {
        return std::nullopt;
    }

    const std::string selectSql = std::string(kSelectFileColumns) +
                                  " FROM files WHERE root_id = ? AND rel_path = ?;";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, selectSql.c_str(), -1, &statement, nullptr) !=
        SQLITE_OK) {
        auto& logger = Logger::instance();
        logger.error("StateDb::getFile: prepare failed: %s", sqlite3_errmsg(db_));
        return std::nullopt;
    }

    sqlite3_bind_int64(statement, 1, rootId);
    sqlite3_bind_text(statement, 2, relPath.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<FileRecord> record;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        record = rowToFileRecord(statement);
    }

    sqlite3_finalize(statement);
    return record;
}

std::vector<FileRecord> StateDb::listFilesByRoot(std::int64_t rootId) {
    std::vector<FileRecord> files;
    if (db_ == nullptr) {
        return files;
    }

    const std::string selectSql = std::string(kSelectFileColumns) +
                                  " FROM files WHERE root_id = ? ORDER BY rel_path;";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, selectSql.c_str(), -1, &statement, nullptr) !=
        SQLITE_OK) {
        auto& logger = Logger::instance();
        logger.error("StateDb::listFilesByRoot: prepare failed: %s",
                     sqlite3_errmsg(db_));
        return files;
    }

    sqlite3_bind_int64(statement, 1, rootId);

    while (sqlite3_step(statement) == SQLITE_ROW) {
        files.push_back(rowToFileRecord(statement));
    }

    sqlite3_finalize(statement);
    return files;
}

std::vector<FileRecord> StateDb::listFilesByStatus(const std::string& syncStatus) {
    std::vector<FileRecord> files;
    if (db_ == nullptr) {
        return files;
    }

    const std::string selectSql = std::string(kSelectFileColumns) +
                                  " FROM files WHERE sync_status = ? ORDER BY root_id, rel_path;";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, selectSql.c_str(), -1, &statement, nullptr) !=
        SQLITE_OK) {
        auto& logger = Logger::instance();
        logger.error("StateDb::listFilesByStatus: prepare failed: %s",
                     sqlite3_errmsg(db_));
        return files;
    }

    sqlite3_bind_text(statement, 1, syncStatus.c_str(), -1, SQLITE_TRANSIENT);

    while (sqlite3_step(statement) == SQLITE_ROW) {
        files.push_back(rowToFileRecord(statement));
    }

    sqlite3_finalize(statement);
    return files;
}

bool StateDb::updateSyncStatus(std::int64_t rootId, const std::string& relPath,
                               const std::string& syncStatus) {
    if (db_ == nullptr) {
        return false;
    }

    auto& logger = Logger::instance();
    const std::int64_t now = currentUnixTime();

    const char* updateSql = R"(
UPDATE files
SET sync_status = ?, updated_at = ?
WHERE root_id = ? AND rel_path = ?;
)";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, updateSql, -1, &statement, nullptr) != SQLITE_OK) {
        logger.error("StateDb::updateSyncStatus: prepare failed: %s",
                     sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_text(statement, 1, syncStatus.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(statement, 2, now);
    sqlite3_bind_int64(statement, 3, rootId);
    sqlite3_bind_text(statement, 4, relPath.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement) != SQLITE_DONE) {
        logger.error("StateDb::updateSyncStatus: step failed: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(statement);
        return false;
    }

    const bool updated = sqlite3_changes(db_) > 0;
    sqlite3_finalize(statement);

    if (!updated) {
        logger.error(
            "StateDb::updateSyncStatus: no row updated root_id=<%lld> rel_path=<%s>",
            static_cast<long long>(rootId), relPath.c_str());
    }

    return updated;
}

bool StateDb::updateTransferProgress(std::int64_t rootId, const std::string& relPath,
                                     std::int64_t bytesTransferred) {
    if (db_ == nullptr) {
        return false;
    }

    if (bytesTransferred < 0) {
        return false;
    }

    auto& logger = Logger::instance();
    const std::int64_t now = currentUnixTime();

    const char* updateSql = R"(
UPDATE files
SET sync_status = ?, bytes_transferred = ?, updated_at = ?
WHERE root_id = ? AND rel_path = ?;
)";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, updateSql, -1, &statement, nullptr) != SQLITE_OK) {
        logger.error("StateDb::updateTransferProgress: prepare failed: %s",
                     sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_text(statement, 1, FileSyncStatus::kSyncing, -1, SQLITE_STATIC);
    sqlite3_bind_int64(statement, 2, bytesTransferred);
    sqlite3_bind_int64(statement, 3, now);
    sqlite3_bind_int64(statement, 4, rootId);
    sqlite3_bind_text(statement, 5, relPath.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement) != SQLITE_DONE) {
        logger.error("StateDb::updateTransferProgress: step failed: %s",
                     sqlite3_errmsg(db_));
        sqlite3_finalize(statement);
        return false;
    }

    const bool updated = sqlite3_changes(db_) > 0;
    sqlite3_finalize(statement);

    if (!updated) {
        logger.error(
            "StateDb::updateTransferProgress: no row updated root_id=<%lld> rel_path=<%s>",
            static_cast<long long>(rootId), relPath.c_str());
    }

    return updated;
}

bool StateDb::deleteFile(std::int64_t rootId, const std::string& relPath) {
    if (db_ == nullptr) {
        return false;
    }

    auto& logger = Logger::instance();

    const char* deleteSql = "DELETE FROM files WHERE root_id = ? AND rel_path = ?;";

    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, deleteSql, -1, &statement, nullptr) != SQLITE_OK) {
        logger.error("StateDb::deleteFile: prepare failed: %s", sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_int64(statement, 1, rootId);
    sqlite3_bind_text(statement, 2, relPath.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(statement) != SQLITE_DONE) {
        logger.error("StateDb::deleteFile: step failed: %s", sqlite3_errmsg(db_));
        sqlite3_finalize(statement);
        return false;
    }

    const bool deleted = sqlite3_changes(db_) > 0;
    sqlite3_finalize(statement);

    if (!deleted) {
        logger.error("StateDb::deleteFile: no row deleted root_id=<%lld> rel_path=<%s>",
                     static_cast<long long>(rootId), relPath.c_str());
    }

    return deleted;
}

}  // namespace fse
