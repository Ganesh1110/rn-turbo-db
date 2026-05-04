# Changelog

All notable changes to this project will be documented in this file.

## [1.4.0] - 2026-05-04

### Added (Reactive Sync Release)

- **Live Queries:** `watchKey(key, cb)` — fires immediately with current value, re-fires on every `set`/`remove` for that key. Returns unsubscribe function.
- **Live Queries:** `watchPrefix(prefix, cb, {debounceMs?})` — re-runs native C++ prefix scan whenever a key within the prefix changes. Smart: only re-queries when the changed key starts with the prefix.
- **Live Queries:** `watchQuery(queryFn, cb, {debounceMs?})` — watches result of any async query function. Debounced (default 100ms) to prevent rapid re-execution on batch writes.
- **Sync Engine — `getLocalChangesAsync(lastClock)`:** Real implementation. Scans all user keys; when `syncEnabled`, reads `SyncMetadata` and returns only dirty records newer than `lastClock`. When sync is disabled, returns all keys for a full initial push.
- **Sync Engine — `applyRemoteChangesAsync(changes)`:** Last-Write-Wins conflict resolution by `updated_at`. Skips records where local version is newer. Applies remote inserts/tombstones with explicit `SyncMetadata` (DIRTY cleared, remote_version recorded).
- **Sync Engine — `markPushedAsync(acks)`:** Clears `SYNC_FLAG_DIRTY` in-place via mmap pointer arithmetic. `msync` called after to durably flush metadata pages. No-op when sync is disabled.
- **Compaction — `compactAsync()`:** New native JSI binding. Auto-skips if fragmentation < 30%. Runs `Compactor::runCompaction()` on the DBWorker thread. `compact()` JS method now delegates to this instead of `repair()`.

### Fixed

- **Critical: `live_bytes_` always 0** — `shouldCompact()` could never return `true` because `live_bytes_` was never incremented. Fixed: `insertRecBytes()` now calls `compactor_->trackLiveBytes(+delta, true)` after every write.
- **Critical: fragmentation grows unbounded on delete** — `remove()` never decremented `live_bytes_`. Fixed: reads the old record size from mmap before tombstoning and calls `trackLiveBytes(-delta, false)`.
- **Critical: stale mmap after compaction** — After atomic rename in `Compactor::runCompaction()`, the mmap still pointed to the old (now unlinked) file descriptor. Fixed: `mmap->close(); mmap->init(db_path_, old_size)` now called immediately after rename. B+Tree offset cache (`btree_`) also invalidated and rebuilt from `pbtree_`.
- **`compact()` JS method called `repair()` instead of the compactor** — Now calls `compactAsync()` native binding with `repair()` as graceful fallback for older builds.

### Changed

- `subscribe(key, cb)` now has a higher-level alias `watchKey(key, cb)` that also fires immediately with the current value.
- `compact()` now triggers real compaction (with fragmentation guard) instead of WAL repair.

---

## [1.3.0] - 2026-05-03

### Added (Data Management Features Release)

- **Native TTL**: `setWithTTLAsync(key, value, ttlMs)` stores expiry as a durable `__ttl:` sidecar key in the C++ engine. `cleanupExpiredAsync()` sweeps all expired sidecar keys natively. Lazy JS-layer check preserved as fallback.
- **True Prefix Search**: `getByPrefixAsync(prefix)` now delegates to native `prefixSearchAsync()` — a proper B+Tree prefix traversal (O(P+M)) instead of the previous `range(prefix, prefix+'\uffff')` hack. `PersistentBPlusTree::prefixSearchWithOffsets()` and `BufferedBTree::prefixSearch()` added.
- **Regex Search**: `regexSearchAsync(pattern)` filters keys using `std::regex` natively. Falls back to JS `RegExp` on web.
- **Native Import/Export**: `exportAsync()` calls `exportDBAsync()` (native B+Tree traversal). `importAsync()` calls `importDBAsync()` (native batch insert). Returns record count.
- **Blob Support**: `setBlobAsync(key, data)` / `getBlobAsync(key)` store raw binary data tagged with a `0xBB` prefix byte, bypassing JSON serialization. Base64 encoding bridges the JSI boundary.
- **Developer Greeting**: `android/build.gradle` prints `[TurboDB] 🔥 Your app is boosted by react-native-turbo-db!` during Gradle configuration. `TurboDB.podspec` prints the equivalent during `pod install`.

### Changed

- `import()` return type changed from `Promise<void>` to `Promise<number>` (number of records imported)
- `setWithTTL()` now calls `ensureInitialized()` to avoid silent no-ops
- `cleanupExpired()` deprecated in favour of `cleanupExpiredAsync()`
- Fixed TypeScript `exports` map: `import` and `require` now use object form with `types` sub-field

---

## [1.2.0] - 2026-04-29

### Added (Performance Engine Release)

- **Data-Level LRU Cache**: Cache deserialized JSI values for hot keys, avoiding repeated mmap reads and BinarySerializer deserialization
- **Zero-Copy Path**: Direct decode for simple types (null, boolean, number, short strings) without full binary deserialization
- **Read-Ahead Prefetch**: Prefetch adjacent B+Tree leaf nodes during range queries to improve sequential read performance
- **Compression Infrastructure**: Added zlib-based compression support for large values (optional, threshold: 256+ bytes)

### Performance Improvements

- Hot key reads now served from LRU cache (avoids mmap + deserialize)
- Simple types bypass serialization (zero-copy decode)
- Range queries benefit from B+Tree leaf prefetching

---

## [1.1.0] - 2026-04-27

### Added (Core Reliability Foundation Release)

- **Delete Integrity**: Persist tombstones to disk, update `pbtree_`, reuse freed offsets
- **Transaction Safety**: `beginTransaction()` / `commitTransaction()` / `rollbackTransaction()` with WAL `TX_BEGIN`/`COMMIT` markers
- **Database Repair**: Real B+Tree corruption detection + repair logic (not stub) — `repair()` fixes corrupted B+Tree headers
- **Concurrent Safety**: Consistent `rw_mutex_` (shared/unique) usage across all read/write paths
- **Batch WAL Writes**: Group multiple writes into single WAL entry + `fsync` via `setMultiAsync()`

---

## [0.1.0] - 2026-04-19

### Added

- Initial release
- JSI-driven embedded key-value database for React Native
- Secure storage with hardware-backed encryption support
- ACID compliance for data integrity
- SSR/isomorphic support
- Web support via index.web.ts

### Features

- Fast key-value storage operations
- Range queries
- Batch operations (setMulti, getMultiple)
- Async variants for all operations
- TypeScript support with full type definitions
