#include "hash/Sha256.hpp"

#include <fstream>
#include <vector>

namespace fse {
namespace {

constexpr std::uint32_t kInitialState[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

constexpr std::uint32_t kRoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr char kHexDigits[] = "0123456789abcdef";

inline std::uint32_t rotr(std::uint32_t value, std::uint32_t bits) {
    return (value >> bits) | (value << (32 - bits));
}

std::string bytesToHex(const std::array<std::uint8_t, 32>& bytes) {
    std::string hex(64, '\0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        hex[i * 2] = kHexDigits[(bytes[i] >> 4) & 0x0f];
        hex[i * 2 + 1] = kHexDigits[bytes[i] & 0x0f];
    }
    return hex;
}

}  // namespace

Sha256Hasher::Sha256Hasher() {
    reset();
}

void Sha256Hasher::reset() {
    for (std::size_t i = 0; i < 8; ++i) {
        state_[i] = kInitialState[i];
    }
    bitCount_ = 0;
    bufferLen_ = 0;
}

void Sha256Hasher::update(const void* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    bitCount_ += static_cast<std::uint64_t>(size) * 8;

    if (bufferLen_ > 0) {
        const std::size_t needed = 64 - bufferLen_;
        if (size < needed) {
            for (std::size_t i = 0; i < size; ++i) {
                buffer_[bufferLen_ + i] = bytes[i];
            }
            bufferLen_ += size;
            return;
        }

        for (std::size_t i = 0; i < needed; ++i) {
            buffer_[bufferLen_ + i] = bytes[i];
        }
        processBlock(buffer_);
        bytes += needed;
        size -= needed;
        bufferLen_ = 0;
    }

    while (size >= 64) {
        processBlock(bytes);
        bytes += 64;
        size -= 64;
    }

    for (std::size_t i = 0; i < size; ++i) {
        buffer_[i] = bytes[i];
    }
    bufferLen_ = size;
}

void Sha256Hasher::update(std::string_view data) {
    update(data.data(), data.size());
}

void Sha256Hasher::processBlock(const std::uint8_t* block) {
    std::uint32_t w[64];

    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<std::uint32_t>(block[i * 4 + 3]);
    }

    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^
                                 (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^
                                 (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ ((~e) & g);
        const std::uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

std::array<std::uint8_t, 32> Sha256Hasher::digest() {
    const std::uint64_t originalBitCount = bitCount_;

    buffer_[bufferLen_++] = 0x80;

    if (bufferLen_ > 56) {
        while (bufferLen_ < 64) {
            buffer_[bufferLen_++] = 0;
        }
        processBlock(buffer_);
        bufferLen_ = 0;
    }

    while (bufferLen_ < 56) {
        buffer_[bufferLen_++] = 0;
    }

    for (int i = 7; i >= 0; --i) {
        buffer_[56 + (7 - i)] =
            static_cast<std::uint8_t>((originalBitCount >> (i * 8)) & 0xff);
    }
    bufferLen_ = 64;
    processBlock(buffer_);

    std::array<std::uint8_t, 32> result{};
    for (std::size_t i = 0; i < 8; ++i) {
        result[i * 4] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xff);
        result[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xff);
        result[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xff);
        result[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xff);
    }

    reset();
    return result;
}

std::string Sha256Hasher::digestHex() {
    return bytesToHex(digest());
}

std::optional<std::string> hashStream(std::istream& input,
                                      std::size_t bufferSize) {
    if (!input || bufferSize == 0) {
        return std::nullopt;
    }

    Sha256Hasher hasher;
    std::vector<char> buffer(bufferSize);

    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize bytesRead = input.gcount();
        if (bytesRead > 0) {
            hasher.update(buffer.data(), static_cast<std::size_t>(bytesRead));
        }
    }

    if (input.bad()) {
        return std::nullopt;
    }

    return hasher.digestHex();
}

std::optional<std::string> hashFile(const std::filesystem::path& path,
                                    std::size_t bufferSize) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    return hashStream(file, bufferSize);
}

}  // namespace fse
