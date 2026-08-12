#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "storage/FileInfo.hpp"

class IStorage {
public:
    virtual ~IStorage() = default;

    virtual bool exist(const std::string& relPath) = 0;
    virtual std::vector<FileInfo> listRecursive() = 0;
    virtual FileInfo stat(const std::string& relPath) = 0;
    virtual std::vector<std::uint8_t> read(const std::string& relPath) = 0;
    virtual bool write(const std::string& relPath,
                       const std::vector<std::uint8_t>& data) = 0;
    virtual std::size_t readRange(const std::string& relPath,
                                  std::int64_t offset,
                                  std::uint8_t* buffer,
                                  std::size_t bufferSize) = 0;
    virtual bool writeRange(const std::string& relPath,
                            std::int64_t offset,
                            const std::uint8_t* data,
                            std::size_t dataSize,
                            bool create = false) = 0;
    virtual bool rename(const std::string& fromRelPath,
                        const std::string& toRelPath) = 0;
    virtual bool createDirs(const std::string& relPath) = 0;
    virtual bool remove(const std::string& relPath) = 0;
};
