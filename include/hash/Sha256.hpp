#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace fse {

class Sha256Hasher {
public:
    Sha256Hasher();

    void reset();
    void update(const void* data, std::size_t size);
    void update(std::string_view data);

    std::array<std::uint8_t, 32> digest();
    std::string digestHex();

private:
    void processBlock(const std::uint8_t* block);

    std::uint32_t state_[8];
    std::uint64_t bitCount_;
    std::uint8_t buffer_[64];
    std::size_t bufferLen_;
};

std::optional<std::string> hashFile(const std::filesystem::path& path,
                                    std::size_t bufferSize = 64 * 1024);

std::optional<std::string> hashStream(std::istream& input,
                                      std::size_t bufferSize = 64 * 1024);

}  // namespace fse
