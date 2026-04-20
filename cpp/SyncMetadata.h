#pragma once

#include <cstdint>

namespace turbo_db {

// Flags for SyncMetadata
constexpr uint8_t SYNC_FLAG_DIRTY     = 0x01; // Record changed locally, needs push
constexpr uint8_t SYNC_FLAG_TOMBSTONE = 0x02; // Record deleted (tombstone)

#pragma pack(push, 1)
/**
 * SyncMetadata (32 bytes)
 * Pre-pended to the payload in MMap when sync is enabled.
 */
struct SyncMetadata {
    uint64_t logical_clock;  // Monotonic tracking clock for OpLog fast-scan
    uint64_t updated_at;     // Physical timestamp for Last-Write-Wins (LWW)
    uint64_t remote_version; // Opaque version string/number from backend
    uint8_t  flags;          // Tombstone, Dirty
    uint8_t  padding[7];     // Align to 8 bytes (32 bytes total)
};
#pragma pack(pop)

} // namespace turbo_db
