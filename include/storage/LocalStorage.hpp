#pragma once

#include "storage/IStorage.hpp"
#include <string>

class LocalStorage : public IStorage {
public:
    explicit LocalStorage(const std::string& root);
    ~LocalStorage() override = default;

    bool exist(const std::string& relPath) override;
    std::vector<FileInfo> listRecursive() override;
    FileInfo stat(const std::string& relPath) override;
    std::vector<std::uint8_t> read(const std::string& relPath) override;
    bool write(const std::string& relPath,
               const std::vector<std::uint8_t>& data) override;
    bool createDirs(const std::string& relPath) override;
    bool remove(const std::string& relPath) override;

private:
    std::string rootPath_;
};
