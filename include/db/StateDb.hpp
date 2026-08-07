#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace fse {

struct RootRecord {
    std::int64_t id;
    std::string path;
    std::string role;
    std::int64_t createdAt;
    std::int64_t updatedAt;
};

struct FileRecord {
    std::int64_t id;
    std::int64_t rootId;
    std::string relPath;
    bool isDirectory;

    std::optional<std::int64_t> mtime;
    std::optional<std::int64_t> size;
    std::optional<std::string> hash;

    std::string syncStatus;

    std::int64_t createdAt;
    std::int64_t updatedAt;
};

namespace FileSyncStatus {
inline constexpr const char* kPending = "pending";
inline constexpr const char* kSynced = "synced";
inline constexpr const char* kFailed = "failed";
inline constexpr const char* kSkipped = "skipped";
inline constexpr const char* kDeleted = "deleted";
}  // namespace FileSyncStatus

class StateDb {
public:
    explicit StateDb(std::string dbPath);
    ~StateDb();

    StateDb(const StateDb&) = delete;
    StateDb& operator=(const StateDb&) = delete;

    bool open();
    void close();
    bool migrate();

    std::int64_t upsertRoot(const std::string& path, const std::string& role);
    std::optional<RootRecord> getRootByPath(const std::string& path);

    std::int64_t upsertFile(const FileRecord& file);
    std::optional<FileRecord> getFile(std::int64_t rootId, const std::string& relPath);
    std::vector<FileRecord> listFilesByRoot(std::int64_t rootId);
    std::vector<FileRecord> listFilesByStatus(const std::string& syncStatus);
    bool updateSyncStatus(std::int64_t rootId, const std::string& relPath,
                          const std::string& syncStatus);
    bool deleteFile(std::int64_t rootId, const std::string& relPath);

    bool isOpen() const;

private:
    sqlite3* db_;
    std::string dbPath_;
};

}  // namespace fse
