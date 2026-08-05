#pragma once

#include "db/StateDb.hpp"
#include "storage/IStorage.hpp"

#include <cstdint>
#include <string>

namespace fse {

class SyncBox {
public:
    SyncBox(StateDb& db,
            IStorage& source,
            IStorage& destination,
            std::int64_t srcRootId,
            const std::string& sourceRootPath);

    void scan();
    void sync();
    void run();

private:
    StateDb& db_;
    IStorage& source_;
    IStorage& destination_;
    std::int64_t srcRootId_;
    std::string sourceRootPath_;
};

}  // namespace fse
