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

bool LocalStorage::write(const std::string& relPath,
                         const std::vector<std::uint8_t>& data) {
    gLogger.debug("LocalStorage::write:Enter relPath=<%s> bytes=<%zu>",
                  relPath.c_str(), data.size());

    if (relPath.empty()) {
        gLogger.debug("LocalStorage::write:Exit (empty relPath)");
        return false;
    }

    const fs::path absPath = makeAbsPath(rootPath_, relPath);
    if (fs::exists(absPath) && fs::is_directory(absPath)) {
        gLogger.debug("LocalStorage::write:Exit (path is a directory)");
        return false;
    }

    const fs::path parentPath = absPath.parent_path();
    if (!parentPath.empty()) {
        fs::create_directories(parentPath);
    }

    std::ofstream file(absPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        gLogger.debug("LocalStorage::write:Exit (failed to open file)");
        return false;
    }

    if (!data.empty()) {
        const std::string content(data.begin(), data.end());
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
    }

    const bool ok = static_cast<bool>(file);
    gLogger.debug("LocalStorage::write:Exit ok=<%d>", ok);
    return ok;
}

std::size_t LocalStorage::readRange(const std::string& relPath,
                                    std::int64_t offset,
                                    std::uint8_t* buffer,
                                    std::size_t bufferSize) {
    gLogger.debug("LocalStorage::readRange:Enter relPath=<%s> offset=<%lld> size=<%zu>",
                  relPath.c_str(), static_cast<long long>(offset), bufferSize);

    if (buffer == nullptr || bufferSize == 0 || offset < 0) {
        gLogger.debug("LocalStorage::readRange:Exit (invalid args)");
        return 0;
    }

    const fs::path absPath = makeAbsPath(rootPath_, relPath);
    std::ifstream file(absPath, std::ios::binary);
    if (!file) {
        gLogger.debug("LocalStorage::readRange:Exit (file not found)");
        return 0;
    }

    file.seekg(offset, std::ios::beg);
    if (!file) {
        gLogger.debug("LocalStorage::readRange:Exit (seek failed)");
        return 0;
    }

    file.read(reinterpret_cast<char*>(buffer),
              static_cast<std::streamsize>(bufferSize));
    if (file.bad()) {
        gLogger.debug("LocalStorage::readRange:Exit (read failed)");
        return 0;
    }

    const std::size_t bytesRead = static_cast<std::size_t>(file.gcount());
    gLogger.debug("LocalStorage::readRange:Exit bytes=<%zu>", bytesRead);
    return bytesRead;
}

bool LocalStorage::writeRange(const std::string& relPath,
                              std::int64_t offset,
                              const std::uint8_t* data,
                              std::size_t dataSize,
                              bool create) {
    gLogger.debug(
        "LocalStorage::writeRange:Enter relPath=<%s> offset=<%lld> size=<%zu> create=<%d>",
        relPath.c_str(), static_cast<long long>(offset), dataSize, create ? 1 : 0);

    if (relPath.empty() || offset < 0) {
        gLogger.debug("LocalStorage::writeRange:Exit (invalid args)");
        return false;
    }

    if (dataSize > 0 && data == nullptr) {
        gLogger.debug("LocalStorage::writeRange:Exit (null data)");
        return false;
    }

    const fs::path absPath = makeAbsPath(rootPath_, relPath);
    if (fs::exists(absPath) && fs::is_directory(absPath)) {
        gLogger.debug("LocalStorage::writeRange:Exit (path is a directory)");
        return false;
    }

    const fs::path parentPath = absPath.parent_path();
    if (!parentPath.empty()) {
        fs::create_directories(parentPath);
    }

    std::ios::openmode mode = std::ios::binary;
    if (create && offset == 0) {
        mode |= std::ios::out | std::ios::trunc;
    } else {
        mode |= std::ios::in | std::ios::out;
    }

    std::fstream file(absPath, mode);
    if (!file) {
        gLogger.debug("LocalStorage::writeRange:Exit (failed to open file)");
        return false;
    }

    if (!(create && offset == 0)) {
        file.seekp(offset, std::ios::beg);
        if (!file) {
            gLogger.debug("LocalStorage::writeRange:Exit (seek failed)");
            return false;
        }
    }

    if (dataSize > 0) {
        file.write(reinterpret_cast<const char*>(data),
                   static_cast<std::streamsize>(dataSize));
    }

    const bool ok = static_cast<bool>(file);
    gLogger.debug("LocalStorage::writeRange:Exit ok=<%d>", ok);
    return ok;
}

bool LocalStorage::rename(const std::string& fromRelPath,
                          const std::string& toRelPath) {
    gLogger.debug("LocalStorage::rename:Enter from=<%s> to=<%s>",
                  fromRelPath.c_str(), toRelPath.c_str());

    if (fromRelPath.empty() || toRelPath.empty()) {
        gLogger.debug("LocalStorage::rename:Exit (empty path)");
        return false;
    }

    const fs::path fromAbs = makeAbsPath(rootPath_, fromRelPath);
    const fs::path toAbs = makeAbsPath(rootPath_, toRelPath);

    if (!fs::exists(fromAbs)) {
        gLogger.debug("LocalStorage::rename:Exit (source missing)");
        return false;
    }

    if (fs::exists(toAbs) && fs::is_directory(toAbs)) {
        gLogger.debug("LocalStorage::rename:Exit (destination is a directory)");
        return false;
    }

    const fs::path parentPath = toAbs.parent_path();
    if (!parentPath.empty()) {
        fs::create_directories(parentPath);
    }

    std::error_code ec;
    fs::rename(fromAbs, toAbs, ec);
    if (ec) {
        gLogger.debug("LocalStorage::rename:Exit (error) <%s>", ec.message().c_str());
        return false;
    }

    gLogger.debug("LocalStorage::rename:Exit ok");
    return true;
}

bool LocalStorage::createDirs(const std::string& relPath) {
    gLogger.debug("LocalStorage::createDirs:Enter relPath=<%s>", relPath.c_str());

    const fs::path absPath = makeAbsPath(rootPath_, relPath);
    if(fs::exists(absPath)){
        gLogger.debug("LocalStorage::createDirs:Directory already present path=<%s>", relPath);
        return true;
    }
    const bool created = fs::create_directories(absPath);

    gLogger.debug("LocalStorage::createDirs:Exit created=<%d>", created);
    return created;
}

bool LocalStorage::remove(const std::string& relPath) {
    gLogger.debug("LocalStorage::remove:Enter relPath=<%s>", relPath.c_str());

    if (relPath.empty()) {
        gLogger.debug("LocalStorage::remove:Exit (empty relPath)");
        return false;
    }

    const fs::path absPath = makeAbsPath(rootPath_, relPath);
    if (!fs::exists(absPath)) {
        gLogger.debug("LocalStorage::remove:Exit (path missing, ok)");
        return true;
    }

    std::error_code ec;
    const bool removed = fs::remove(absPath, ec);
    if (ec) {
        gLogger.debug("LocalStorage::remove:Exit (error) <%s>", ec.message().c_str());
        return false;
    }

    gLogger.debug("LocalStorage::remove:Exit removed=<%d>", removed);
    return true;
}
