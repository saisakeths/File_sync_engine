#pragma once

#include "db/StateDb.hpp"
#include "storage/IStorage.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace fse {

struct ChunkCopyResult {
    bool ok = false;
    std::int64_t bytesWritten = 0;
    std::string error;
};

// Temp file convention for in-progress chunked copies (see Phase 5 integration).
std::string makeTempRelPath(const std::string& relPath);

ChunkCopyResult copyFileChunked(IStorage& source,
                                IStorage& destination,
                                const FileRecord& file,
                                std::size_t chunkSize,
                                std::int64_t startOffset,
                                const std::string& tempRelPath,
                                StateDb& db,
                                std::int64_t persistEveryChunks = 1);

}  // namespace fse
