#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

struct FileInfo {
    std::string relPath;
    std::uintmax_t size;
    std::filesystem::file_time_type lastModified;
    std::string status;
    bool isDirectory;
};
