#include "utils/TimeUtils.hpp"

#include <chrono>

namespace fse {

std::optional<std::int64_t> fileTimeToUnixSeconds(
    const std::filesystem::file_time_type& fileTime) {
    using namespace std::chrono;

    const auto systemTime = time_point_cast<system_clock::duration>(
        fileTime - std::filesystem::file_time_type::clock::now() +
        system_clock::now());

    return duration_cast<seconds>(systemTime.time_since_epoch()).count();
}

}  // namespace fse
