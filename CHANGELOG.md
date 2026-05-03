# Changelog

All notable changes to this project will be documented in this file.

## [1.3.0] - 2026-05-03

### Added (Data Management Features Release - v1.3.0)

- **Native TTL**: `setWithTTLAsync(key, value, ttlMs)` stores expiry as a durable `__ttl:` sidecar key in the C++ engine. `cleanupExpiredAsync()` sweeps all expired sidecar keys natively. Lazy JS-layer check preserved as fallback.
- **True Prefix Search**: `getByPrefixAsync(prefix)` now delegates to native `prefixSearchAsync()` — a proper B+Tree prefix traversal (O(P+M)) instead of the previous `range(prefix, prefix+'\uffff')` hack. `PersistentBPlusTree::prefixSearchWithOffsets()` and `BufferedBTree::prefixSearch()` added.
- **Regex Search**: `regexSearchAsync(pattern)` filters keys using `std::regex` natively. Falls back to JS `RegExp` on web.
- **Native Import/Export**: `export()` now calls `exportDBAsync()` (native B+Tree traversal). `import()` calls `importDBAsync()` (native batch insert). Returns record count instead of void.
- **Blob Support**: `setBlobAsync(key, data)` / `getBlobAsync(key)` store raw binary data tagged with a `0xBB` prefix byte, bypassing JSON serialization. Base64 encoding bridges the JSI boundary.
- **Developer Greeting**: `android/build.gradle` prints `[TurboDB] 🔥 Your app is boosted by react-native-turbo-db!` in green during Gradle configuration. `TurboDB.podspec` prints the equivalent during `pod install`.

### API Changes

- `import()` return type changed from `Promise<void>` to `Promise<number>` (number of records imported)
- `setWithTTL()` now calls `ensureInitialized()` to avoid silent no-ops
- `cleanupExpired()` deprecated in favour of `cleanupExpiredAsync()`

## [1.2.0] - 2026-04-29


### Added (Performance Release - v1.2.0)

- **Data-Level LRU Cache**: Cache deserialized JSI values for hot keys, avoiding repeated mmap reads and BinarySerializer deserialization
- **Zero-Copy Path**: Direct decode for simple types (null, boolean, number, short strings) without full binary deserialization
- **Read-Ahead Prefetch**: Prefetch adjacent B+Tree leaf nodes during range queries to improve sequential read performance
- **Compression Infrastructure**: Added zlib-based compression support for large values (optional, threshold: 256+ bytes)

### Performance Improvements

- Hot key reads now served from LRU cache (avoids mmap + deserialize)
- Simple types bypass serialization (zero-copy decode)
- Range queries benefit from B+Tree leaf prefetching

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
