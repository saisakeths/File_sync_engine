#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace fse {

std::optional<std::int64_t> fileTimeToUnixSeconds(
    const std::filesystem::file_time_type& fileTime);

}  // namespace fse
