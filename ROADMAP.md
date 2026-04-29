# TurboDB Release Roadmap

> **Vision:** Build the fastest, most reliable embedded database for React Native — from a trustworthy KV store to a full-featured reactive SQL-ready engine.

---

## Visual Progression

```
R1 Trust        → Correctness & Durability
R2 Speed        → Performance & Efficiency
R3 Features     → Richer KV Capabilities
R4 Reactive     → Sync & Live Database
R5 Security     → Enterprise Readiness
R6 SQL          → Next-Gen Query Engine
```

---

## Release 1 — Core Reliability Foundation

**Version:** `v1.1.0` (Reliability Release)  
**Focus:** Correctness + Durability  
**Outcome:** "Production-safe core engine"

### Features

| Feature | Current Status | Confidence | Priority |
|---------|---------------|------------|----------|
| Delete Integrity Fix | ✅ Fixed | 8/10 | P0 |
| Transaction Safety (atomic batches) | ✅ Implemented | 8/10 | P0 |
| Database Repair (real, not stub) | ✅ Implemented | 7/10 | P0 |
| Concurrent Read/Write Safety | ✅ Fixed | 8/10 | P0 |
| Batch WAL Writes | ✅ Implemented | 7/10 | P1 |

### Key Deliverables

1. **Delete Integrity** — Persist tombstones to disk, update `pbtree_`, reuse freed offsets ✅
2. **Transaction Safety** — `beginTransaction()` / `commit()` / `rollback()` with WAL `TX_BEGIN`/`COMMIT` markers ✅
3. **Database Repair** — Real B+Tree corruption detection + repair logic (not stub) ✅
4. **Concurrent Safety** — Consistent `rw_mutex_` usage across all read/write paths ✅
5. **Batch WAL Writes** — Group multiple writes into single WAL entry + `fsync` ✅

### Success Criteria

- [x] `remove()` persists across app restart
- [x] Multi-key transactions are atomic (all-or-nothing)
- [x] `repair()` actually fixes corrupted B+Tree headers
- [x] No race conditions under concurrent `setAsync` / `getAsync`
- [ ] WAL batching reduces `fsync` calls by 10x (implemented, needs testing)

---

## Release 2 — Performance Engine

**Version:** `v1.2.0` (Performance Release)  
**Focus:** Speed + Storage Efficiency  
**Outcome:** "Big performance jump"

### Features

| Feature | Current Status | Confidence | Priority |
|---------|---------------|------------|----------|
| Data-Level LRU Cache | ⚠️ Nodes only | 3/10 | P0 |
| Read-Ahead / Prefetch | ❌ Missing | 0/10 | P1 |
| Zero-Copy / Memory Views | ❌ Missing | 2/10 | P0 |
| Parallel B+Tree Flushing | ❌ Missing | 2/10 | P1 |
| Compression (LZ4/zstd) | ❌ Missing | 0/10 | P1 |

### Key Deliverables

1. **Data-Level LRU Cache** — Cache deserialized JSI values for hot keys (separate from B+Tree node cache)
2. **Zero-Copy Path** — Direct mmap → JSI for simple types (strings, numbers) without `BinarySerializer`
3. **BufferedBTree Rewrite** — True buffered writes (not just index batching)
4. **Read-Ahead** — Prefetch adjacent B+Tree leaves for range queries
5. **Compression** — Optional LZ4 for cold data / large values

### Success Criteria

- [ ] Hot key reads avoid mmap + deserialize (serve from LRU)
- [ ] Range queries 5x faster with read-ahead
- [ ] Simple types bypass serialization (zero-copy)
- [ ] Buffered writes buffer DATA, not just index
- [ ] Compression reduces file size by 40-60% for compressible data

---

## Release 3 — Data Management Features

**Version:** `v1.3.0` (Data Features Release)  
**Focus:** Richer KV Database Behavior  
**Outcome:** "KV store becomes feature-rich database"

### Features

| Feature | Current Status | Confidence | Priority |
|---------|---------------|------------|----------|
| Native TTL / Expiration | ⚠️ Web only | 4/10 | P0 |
| Native Prefix Search | ⚠️ Via range hack | 5/10 | P0 |
| Regex Search | ❌ Missing | 0/10 | P1 |
| Import / Export | ⚠️ Web only | 3/10 | P0 |
| Blob Support (formalize) | ⚠️ Implicit | 5/10 | P0 |

### Key Deliverables

1. **Native TTL** — Store expiry timestamp with records, lazy expiry on `findRec()`, background cleanup
2. **True Prefix Search** — Dedicated `prefixSearch()` using B+Tree prefix traversal (not range hack)
3. **Regex Search** — Optional regex filter for keys (compile-time flag)
4. **Import/Export** — Native JSON export/import using B+Tree traversal
5. **Blob Support** — Formal `setBlob()` / `getBlob()` with streaming support

### Success Criteria

- [ ] TTL keys auto-expire and are cleaned up
- [ ] `prefixSearch("user_")` 10x faster than range query hack
- [ ] Regex search works on keys (e.g., `.*\d{3}$`)
- [ ] Export produces valid JSON, import restores all data
- [ ] Blob API supports values > 1MB

---

## Release 4 — Observability + Scale

**Version:** `v1.4.0` (Reactive Sync Release)  
**Focus:** Advanced Runtime Behavior  
**Outcome:** "Moves toward embedded realtime database"

### Features

| Feature | Current Status | Confidence | Priority |
|---------|---------------|------------|----------|
| Change Events / Observable API | ⚠️ Web only | 3/10 | P0 |
| Live Queries | ❌ Missing | 1/10 | P0 |
| Offline-First Sync Engine | 🔍 Stubs only | 1/10 | P0 |
| Compaction Rewrite (fully correct) | 🔍 Buggy | 4/10 | P0 |
| Write Buffer Hardening | ⚠️ Partial | 6/10 | P1 |

### Key Deliverables

1. **Change Events** — Native event emitter for `set` / `remove` / `batch` operations
2. **Live Queries** — Re-execute queries when dependent keys change (reactive)
3. **Sync Engine** — Real `getLocalChanges()` / `applyRemoteChanges()` with CRDT-like merging
4. **Compaction Rewrite** — Correct compaction with mmap remapping, `live_bytes_` tracking
5. **Offline Queue** — Queue mutations when offline, sync when online

### Success Criteria

- [ ] `subscribe(key, callback)` fires on native writes
- [ ] Live queries auto-update React components
- [ ] Sync engine handles conflicts (last-write-wins or custom merge)
- [ ] Compaction actually shrinks file and remaps correctly
- [ ] Offline mutations queue and replay on reconnect

---

## Release 5 — Security & Enterprise

**Version:** `v1.5.0` (Security Release)  
**Focus:** Enterprise Readiness  
**Outcome:** "Enterprise/security story"

### Features

| Feature | Current Status | Confidence | Priority |
|---------|---------------|------------|----------|
| Async Encryption Fix + Real Background | 🔍 Broken | 3/10 | P0 |
| Key Rotation | ❌ Missing | 0/10 | P0 |
| HMAC Verification | ❌ Missing (uses CRC) | 1/10 | P0 |
| Per-Key Selective Encryption | ❌ Missing | 0/10 | P1 |
| Corruption Recovery Enhancements | ⚠️ WAL only | 5/10 | P1 |

### Key Deliverables

1. **Async Encryption Fix** — `setAsync()` Promise actually waits for scheduler + encryption
2. **Key Rotation** — Re-encrypt entire database with new key without downtime
3. **HMAC Verification** — Replace CRC32 with HMAC-SHA256 for integrity
4. **Selective Encryption** — Per-key `encrypt: false` flag to skip encryption
5. **Recovery Enhancements** — B+Tree node repair, orphaned record cleanup

### Success Criteria

- [ ] `setAsync()` resolves ONLY after write is durable
- [ ] Key rotation completes without app restart
- [ ] HMAC prevents tampering (not just accidental corruption)
- [ ] Unencrypted keys bypass crypto overhead
- [ ] Recovery fixes corrupted nodes, not just WAL replay

---

## Release 6 — Query Layer Expansion

**Version:** `v2.0.0` (Query Engine Release)  
**Focus:** Stretch Feature  
**Outcome:** "Next-generation query layer"

### Features

| Feature | Current Status | Confidence | Priority |
|---------|---------------|------------|----------|
| SQL Query Layer / Schema / JOIN-lite | ❌ Missing | 0/10 | P0 |

### Key Deliverables

1. **SQL Parser** — Lightweight SQL parser for `SELECT`, `INSERT`, `UPDATE`, `DELETE`
2. **Table Schema** — Define structured tables with typed columns
3. **JOIN Operations** — Basic `INNER JOIN` / `LEFT JOIN` support
4. **Index Support** — Secondary indexes on table columns
5. **Migration System** — Schema versioning and migration scripts

### Success Criteria

- [ ] `db.query("SELECT * FROM users WHERE age > 21")` works
- [ ] `db.createTable("users", {id: "INTEGER", name: "TEXT"})` defines schema
- [ ] `JOIN` queries return merged results
- [ ] Secondary indexes accelerate WHERE clauses
- [ ] Schema migrations run automatically on version bump

---

## Release Timeline (Estimated)

| Release | Version | Focus | ETA |
|---------|---------|-------|-----|
| R1 | v1.1.0 | Core Reliability | Week 1-2 |
| R2 | v1.2.0 | Performance Engine | Week 3-4 |
| R3 | v1.3.0 | Data Features | Week 5-6 |
| R4 | v1.4.0 | Reactive Sync | Week 7-9 |
| R5 | v1.5.0 | Security & Enterprise | Week 10-12 |
| R6 | v2.0.0 | SQL Query Engine | Week 13-16 |

---

## Contributing

Each release has clearly defined deliverables and success criteria.  
Pick a release, check the "Key Deliverables" and "Success Criteria" sections, and submit a PR!

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup and guidelines.

---

_Last updated: April 2026_
