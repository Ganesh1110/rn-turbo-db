# TurboDB Features Pending

> A roadmap of upcoming features and performance improvements for TurboDB.

---

## 🚀 Performance Optimizations (Priority: High)

### In-Memory Caching Layer

- [ ] **LRU Cache** - Add in-memory LRU cache for hot keys (reduce disk I/O)
- [ ] **Write Buffer** - Batch writes in memory, flush periodically (reduce mmap sync)
- [ ] **Read-Ahead** - Prefetch adjacent data for range queries

### Zero-Copy Optimizations

- [ ] **Avoid Arena Serialization** - Direct JSI → mmap path for simple types
- [ ] **Skip Encryption for Non-Sensitive Data** - Add flag to bypass encryption
- [ ] **Memory Views** - Use mmap memory views instead of copies

### Threading Improvements

- [ ] **Async Encryption** - Move crypto to background thread
- [ ] **Parallel B+Tree Flushing** - Flushing BTree operations in parallel
- [ ] **Batch WAL Writes** - Combine multiple WAL entries into single fsync

### Benchmark Targets

| Current          | Target | Improvement |
| ---------------- | ------ | ----------- |
| ~10ms bulk write | ~2ms   | 5x faster   |
| ~4ms random read | ~0.5ms | 8x faster   |

---

## 📦 Feature Enhancements (Priority: Medium)

### Data Types

- [ ] **Blob Support** - Store binary data (images, files)
- [ ] **TTL/Expiration** - Auto-expire keys with timestamp
- [ ] **Versioning** - Keep history of key changes

### Query Capabilities

- [ ] **Prefix Search** - Find keys matching prefix
- [ ] **Regex Search** - Filter keys by pattern
- [ ] **Full-Text Index** - Search within stored strings

### Structured Data (SQL-like)

- [ ] **SQL Query Layer** - Support SQL-like queries for complex data structures
- [ ] **Table Schema** - Define structured tables with columns and types
- [ ] **JOIN Operations** - Support basic relational JOINs

### Data Management

- [ ] **Import/Export** - JSON backup and restore
- [ ] **Compression** - LZ4/zstd compression for cold data
- [ ] **Database Repair** - Automatic corruption detection and repair

### Observability

- [ ] **Change Events** - Subscribe to key changes
- [ ] **Metrics API** - Query cache hits, disk I/O, etc.
- [ ] **Debug Mode** - Verbose logging for troubleshooting

### Real-Time Sync

- [ ] **Live Queries** - Reactive queries that auto-update on data changes
- [ ] **Observable API** - Subscribe to specific keys or query results
- [ ] **Offline-First** - Queue operations when offline, sync when online

---

## 🔒 Security Enhancements (Priority: Low)

- [ ] **Key Rotation** - Rotate master key without data loss
- [ ] **HMAC Verification** - Additional integrity layer
- [ ] **Encrypted Queries** - Support encrypted key lookups

---

## 🌍 Platform Extensions (Priority: Low)

- [ ] **React Native Windows** - Add Windows UWP support
- [ ] **React Native macOS** - Native macOS Catalyst support
- [ ] **Server-Side Storage** - Node.js adapter using filesystem

---

## 📋 Backlog (Icebox)

- [ ] ACID transactions across multiple keys
- [ ] Replication/Cluster support
- [ ] Graph/Relationship data model

---

## Contributing

Want to contribute? Check out our [Contributing Guide](CONTRIBUTING.md) and help us build the fastest React Native database!

---

_Last updated: April 2026_
