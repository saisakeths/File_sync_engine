#include "storage/LocalStorage.hpp"

#include "logger.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace {

auto& gLogger = fse::Logger::instance();

fs::path makeAbsPath(const std::string& rootPath, const std::string& relPath) {
    return fs::path(rootPath) / relPath;
}

}  // namespace

LocalStorage::LocalStorage(const std::string& root) : rootPath_(root) {
    gLogger.debug("LocalStorage::LocalStorage:Enter");
    gLogger.debug("LocalStorage::LocalStorage:Exit rootPath=<%s>", rootPath_.c_str());
}

bool LocalStorage::exist(const std::string& relPath) {
    gLogger.debug("LocalStorage::exist:Enter relPath=<%s>", relPath.c_str());
    const fs::path absPath = makeAbsPath(rootPath_, relPath);
    gLogger.debug("LocalStorage::exist:absPath=<%s>", absPath.string().c_str());
    return fs::exists(absPath);
}

std::vector<FileInfo> LocalStorage::listRecursive() {
    gLogger.debug("LocalStorage::listRecursive:Enter");

    std::vector<FileInfo> result;
    const fs::path root(rootPath_);
    if (!fs::exists(root)) {
        gLogger.debug("LocalStorage::listRecursive:Exit (root missing)");
        return result;
    }

    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        FileInfo info;
        info.relPath = fs::relative(entry.path(), root).generic_string();
        info.isDirectory = entry.is_directory();
        info.size = info.isDirectory ? 0 : entry.file_size();
        info.lastModified = entry.last_write_time();
        info.status = info.isDirectory ? "directory" : "file";
        result.push_back(std::move(info));
    }

    gLogger.debug("LocalStorage::listRecursive:Exit count=<%zu>", result.size());
    return result;
}

FileInfo LocalStorage::stat(const std::string& relPath) {
    gLogger.debug("LocalStorage::stat:Enter relPath=<%s>", relPath.c_str());

    const fs::path absPath = makeAbsPath(rootPath_, relPath);
    FileInfo info;
    info.relPath = relPath;
    info.isDirectory = fs::is_directory(absPath);

    if (fs::exists(absPath) && !info.isDirectory) {
        info.size = fs::file_size(absPath);
    } else {
        info.size = 0;
    }

    if (fs::exists(absPath)) {
        info.lastModified = fs::last_write_time(absPath);
        info.status = info.isDirectory ? "directory" : "file";
    } else {
        info.status = "missing";
    }

    gLogger.debug("LocalStorage::stat:Exit status=<%s>", info.status.c_str());
    return info;
}

std::vector<std::uint8_t> LocalStorage::read(const std::string& relPath) {
    gLogger.debug("LocalStorage::read:Enter relPath=<%s>", relPath.c_str());

    const fs::path absPath = makeAbsPath(rootPath_, relPath);
    std::ifstream file(absPath, std::ios::binary);
    if (!file) {
        gLogger.debug("LocalStorage::read:Exit (file not found)");
        return {};
    }

    std::vector<std::uint8_t> data(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    gLogger.debug("LocalStorage::read:Exit bytes=<%zu>", data.size());
    return data;
}

bool LocalStorage::createDirs(const std::string& relPath) {
    gLogger.debug("LocalStorage::createDirs:Enter relPath=<%s>", relPath.c_str());

    const fs::path absPath = makeAbsPath(rootPath_, relPath);
    const bool created = fs::create_directories(absPath);

    gLogger.debug("LocalStorage::createDirs:Exit created=<%d>", created);
    return created;
}
