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
    virtual bool createDirs(const std::string& relPath) = 0;
    virtual bool remove(const std::string& relPath) = 0;
};
