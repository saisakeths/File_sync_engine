#include "sync/ChunkCopier.hpp"

#include "hash/Sha256.hpp"
#include "logger.hpp"

#include <algorithm>
#include <vector>

namespace fse {

namespace {

constexpr const char* kTempSuffix = ".fse.tmp";

bool hashSourceRange(IStorage& source,
                     const std::string& relPath,
                     std::int64_t offset,
                     std::int64_t endOffset,
                     std::size_t chunkSize,
                     Sha256Hasher& hasher,
                     std::vector<std::uint8_t>& buffer,
                     std::string& error) {
    while (offset < endOffset) {
        const std::int64_t remaining = endOffset - offset;
        const std::size_t toRead = static_cast<std::size_t>(
            std::min<std::int64_t>(remaining, static_cast<std::int64_t>(chunkSize)));

        const std::size_t bytesRead =
            source.readRange(relPath, offset, buffer.data(), toRead);
        if (bytesRead == 0) {
            error = "unexpected EOF while hashing source";
            return false;
        }

        hasher.update(buffer.data(), bytesRead);
        offset += static_cast<std::int64_t>(bytesRead);
    }

    return true;
}

}  // namespace

std::string makeTempRelPath(const std::string& relPath) {
    return relPath + kTempSuffix;
}

ChunkCopyResult copyFileChunked(IStorage& source,
                                IStorage& destination,
                                const FileRecord& file,
                                std::size_t chunkSize,
                                std::int64_t startOffset,
                                const std::string& tempRelPath,
                                StateDb& db,
                                std::int64_t persistEveryChunks) {
    auto& logger = Logger::instance();
    ChunkCopyResult result;

    if (file.isDirectory) {
        result.error = "cannot chunk-copy a directory";
        return result;
    }

    if (!file.size.has_value()) {
        result.error = "file size is unknown";
        return result;
    }

    const std::int64_t fileSize = *file.size;
    if (startOffset < 0 || startOffset > fileSize) {
        result.error = "invalid resume offset";
        return result;
    }

    if (chunkSize == 0) {
        result.error = "chunk size must be greater than zero";
        return result;
    }

    if (persistEveryChunks < 1) {
        persistEveryChunks = 1;
    }

    std::vector<std::uint8_t> buffer(chunkSize);
    Sha256Hasher hasher;

    if (startOffset > 0) {
        if (!hashSourceRange(source, file.relPath, 0, startOffset, chunkSize,
                             hasher, buffer, result.error)) {
            logger.error("ChunkCopier::copyFileChunked: hash resume prefix failed for <%s>: %s",
                         file.relPath.c_str(), result.error.c_str());
            return result;
        }
    }

    if (fileSize == 0) {
        if (!destination.writeRange(tempRelPath, 0, nullptr, 0, true)) {
            result.error = "failed to create empty temp file";
            logger.error("ChunkCopier::copyFileChunked: %s for <%s>",
                         result.error.c_str(), file.relPath.c_str());
            return result;
        }
    } else {
        std::int64_t offset = startOffset;
        std::int64_t chunksSincePersist = 0;

        while (offset < fileSize) {
            const std::int64_t remaining = fileSize - offset;
            const std::size_t toRead = static_cast<std::size_t>(
                std::min<std::int64_t>(remaining, static_cast<std::int64_t>(chunkSize)));

            const std::size_t bytesRead =
                source.readRange(file.relPath, offset, buffer.data(), toRead);
            if (bytesRead == 0) {
                result.error = "unexpected EOF while reading source";
                logger.error("ChunkCopier::copyFileChunked: %s for <%s> at offset <%lld>",
                             result.error.c_str(), file.relPath.c_str(),
                             static_cast<long long>(offset));
                return result;
            }

            const bool create = (offset == 0);
            if (!destination.writeRange(tempRelPath, offset, buffer.data(), bytesRead,
                                        create)) {
                result.error = "failed to write chunk to temp file";
                logger.error("ChunkCopier::copyFileChunked: %s for <%s> at offset <%lld>",
                             result.error.c_str(), file.relPath.c_str(),
                             static_cast<long long>(offset));
                return result;
            }

            hasher.update(buffer.data(), bytesRead);
            offset += static_cast<std::int64_t>(bytesRead);
            ++chunksSincePersist;

            if (chunksSincePersist >= persistEveryChunks) {
                if (!db.updateTransferProgress(file.rootId, file.relPath, offset)) {
                    logger.warning(
                        "ChunkCopier::copyFileChunked: failed to persist progress for <%s> at <%lld>",
                        file.relPath.c_str(), static_cast<long long>(offset));
                }
                chunksSincePersist = 0;
            }
        }

        if (chunksSincePersist > 0) {
            if (!db.updateTransferProgress(file.rootId, file.relPath, fileSize)) {
                logger.warning(
                    "ChunkCopier::copyFileChunked: failed to persist final progress for <%s>",
                    file.relPath.c_str());
            }
        }
    }

    if (file.hash.has_value()) {
        const std::string digest = hasher.digestHex();
        if (digest != *file.hash) {
            result.error = "hash mismatch after copy";
            logger.error("ChunkCopier::copyFileChunked: hash mismatch for <%s>",
                         file.relPath.c_str());
            return result;
        }
    }

    result.ok = true;
    result.bytesWritten = fileSize;
    logger.info("ChunkCopier::copyFileChunked: copied <%s> (%lld bytes)",
                file.relPath.c_str(), static_cast<long long>(fileSize));
    return result;
}

}  // namespace fse
