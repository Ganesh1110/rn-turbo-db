#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <functional>

namespace turbo_db {

class MMapRegion;
class BufferedBTree;
class SecureCryptoContext;
class DBEngine;

class Compactor {
public:
    static constexpr double FRAGMENTATION_THRESHOLD = 0.30;

    explicit Compactor(DBEngine* engine, MMapRegion* mmap, BufferedBTree* btree, std::string db_path, SecureCryptoContext* crypto);

    double getFragmentationRatio(size_t live_bytes, size_t total_file_bytes) const;
    bool shouldCompact(size_t live_bytes, size_t total_file_bytes) const;
    void runCompaction(MMapRegion* mmap, class PersistentBPlusTree* tree, std::function<void(bool success, size_t bytes_saved)> onDone);
    void trackLiveBytes(size_t delta, bool add);
    size_t getLiveBytes() const { return live_bytes_.load(std::memory_order_relaxed); }

private:
    DBEngine* engine_;
    MMapRegion* mmap_;
    BufferedBTree* btree_;
    std::string db_path_;
    SecureCryptoContext* crypto_;
    std::atomic<size_t> live_bytes_{0};
};

} // namespace turbo_db
