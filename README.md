# TurboDB

<p align="center">
  <a href="https://www.npmjs.com/package/react-native-turbo-db">
    <img src="https://img.shields.io/npm/v/react-native-turbo-db.svg" alt="npm version" />
  </a>
  <a href="https://www.npmjs.com/package/react-native-turbo-db">
    <img src="https://img.shields.io/npm/dm/react-native-turbo-db.svg" alt="npm downloads" />
  </a>
  <a href="https://github.com/ganeshjayaprakash/react-native-turbo-db/blob/main/LICENSE">
    <img src="https://img.shields.io/npm/l/react-native-turbo-db.svg" alt="MIT License" />
  </a>
  <a href="https://github.com/ganeshjayaprakash/react-native-turbo-db/actions">
    <img src="https://img.shields.io/github/actions/workflow/status/ganeshjayaprakash/react-native-turbo-db/ci.yml" alt="CI Status" />
  </a>
  <a href="https://reactnative.dev/">
    <img src="https://img.shields.io/badge/React%20Native-0.85-blue.svg" alt="React Native" />
  </a>
</p>

A **high-performance, encrypted embedded database** for React Native built on C++, leveraging the New Architecture (JSI/TurboModules) for native-speed operations.

## Why TurboDB?

Most React Native storage solutions rely on AsyncStorage (slow, async-only) or complex SQLite wrappers. TurboDB is purpose-built for the New Architecture:

- **JSI-Native Speed** — Direct C++ access without the async bridge overhead. Synchronous reads in your render function, zero "flash of missing content".
- **Zero Serialization** — Native B+Tree storage eliminates `JSON.stringify` overhead on every write.
- **Military-Grade Encryption** — All data encrypted at rest using XChaCha20-Poly1305 (libsodium), backed by hardware keystores (iOS Keychain / Android Keystore).
- **ACID Compliant** — Write-Ahead Logging (WAL) ensures your data survives crashes and power loss.
- **Isomorphic** — Same API works on React Native, Web (IndexedDB), and SSR frameworks (Next.js, Remix).

## What's New in v1.1.0 (Core Reliability Engine)

TurboDB v1.1.0 is a massive production-hardening release, focused entirely on data integrity and correctness:

- **10x Faster WAL Batching** — `setMultiAsync` now intelligently defers `fsync` calls until the end of the batch, massively reducing disk I/O latency.
- **Atomic Transactions** — True transactional safety across the C++ engine and B+Tree layers. If an error occurs, nothing is partially written.
- **Delete Integrity** — Tombstones safely persist to disk, permanently preventing zombie data, while dynamically reusing freed memory offsets.
- **Auto-Repair Engine** — The `repair()` method now actively identifies and fixes corrupted B+Tree headers or missing nodes on the fly.
- **Thread-Safety** — Bulletproof C++ `rw_mutex_` concurrency guards prevent any race conditions during heavy parallel `getAsync` and `setAsync` traffic.

## Features

### Core Capabilities

- ⚡ **Synchronous reads** — Access data instantly, no await needed
- 🔐 **End-to-end encryption** — XChaCha20-Poly1305 with hardware-backed keys
- 💾 **ACID transactions** — WAL ensures data integrity
- 🗜️ **B+Tree indexing** — Fast range queries and pagination

### Advanced

- 📡 **Offline-first sync** — Built-in SyncManager for remote synchronization
- 🔑 **Secure enclave storage** — Hardware-protected secrets (PINs, tokens)
- ⏱️ **TTL support** — Auto-expiring keys
- 📊 **Metrics & diagnostics** — Health checks, compaction, stats

## Installation

```bash
npm install react-native-turbo-db
# or
yarn add react-native-turbo-db
```

### iOS

```bash
cd ios && pod install
```

### Android

No additional setup required. Ensure you have the New Architecture enabled:

```properties
# android/gradle.properties
newArchEnabled=true
```

### Web / SSR

Works out of the box with IndexedDB:

```bash
npm install react-native-turbo-db
```

```tsx
// next.config.js (for SSR compatibility)
module.exports = {
  webpack: (config) => {
    config.resolve.alias = {
      ...config.resolve.alias,
      'react-native': 'react-native-turbo-db',
    };
    return config;
  },
};
```

## Quick Start

```tsx
import { TurboDB } from 'react-native-turbo-db';

const db = await TurboDB.create('my_app', 10 * 1024 * 1024);

// Synchronous - instant reads, no await
db.set('user', { name: 'Alice', role: 'admin' });
const user = db.get('user'); // { name: 'Alice', role: 'admin' }

// Asynchronous - for large operations (runs on DBWorker thread)
await db.setAsync('settings', { theme: 'dark', notifications: true });
const settings = await db.getAsync('settings');
```

## API Reference

### Initialization

#### `TurboDB.create(path, size?, options?)`

Creates and initializes a new database instance.

| Parameter             | Type      | Default            | Description                |
| --------------------- | --------- | ------------------ | -------------------------- |
| `path`                | `string`  | —                  | Full path to database file |
| `size`                | `number`  | `10 * 1024 * 1024` | Initial file size in bytes |
| `options.syncEnabled` | `boolean` | `false`            | Enable Write-Ahead Logging |

```tsx
const db = await TurboDB.create(
  `${TurboDB.getDocumentsDirectory()}/app.db`,
  10 * 1024 * 1024,
  { syncEnabled: true }
);
```

---

### Synchronous API

> **Note**: These methods block the JS thread briefly. For very large values (>1MB), use async variants.

#### `db.set(key, value)` → `boolean`

Set a value. Returns `true` on success.

```tsx
db.set('key', { foo: 'bar' });
```

#### `db.get<T>(key)` → `T | undefined`

Get a value. Returns `undefined` if not found.

```tsx
const user = db.get<User>('user');
if (user) {
  console.log(user.name);
}
```

#### `db.has(key)` → `boolean`

Check if a key exists.

```tsx
const exists = db.has('user');
```

#### `db.remove(key)` / `db.del(key)` → `boolean`

Delete a key.

```tsx
db.remove('user');
// or
db.del('user');
```

#### `db.setMulti(entries)` → `boolean`

Atomic batch write.

```tsx
db.setMulti({
  token: 'abc123',
  refresh: 'xyz789',
  expires: Date.now() + 3600000,
});
```

#### `db.getMultiple(keys)` → `Record<string, any>`

Batch read multiple keys.

```tsx
const results = db.getMultiple(['user', 'settings', 'theme']);
// { user: {...}, settings: {...}, theme: {...} }
```

#### `db.getAllKeys()` → `string[]`

Get all user keys (excludes internal keys).

```tsx
const keys = db.getAllKeys();
```

#### `db.getAllKeysPaged(limit, offset)` → `string[]`

Paginated key enumeration for large datasets.

```tsx
// First 100 keys
const page1 = db.getAllKeysPaged(100, 0);
// Next 100 keys
const page2 = db.getAllKeysPaged(100, 100);
```

#### `db.rangeQuery(startKey, endKey)` → `RangeQueryResult[]`

Range query for lexicographically ordered keys.

```tsx
// Get all logs from 2024-01-01 to 2024-12-31
const logs = db.rangeQuery('log_2024-01-01', 'log_2024-12-31');
// [{ key: 'log_2024-01-01', value: {...} }, ...]
```

#### `db.getByPrefix(prefix)` → `RangeQueryResult[]`

Get all records with keys starting with prefix.

```tsx
const users = db.getByPrefix('user:');
// All keys starting with 'user:'
```

#### `db.clear()` / `db.deleteAll()` → `boolean`

Clear all data synchronously.

```tsx
db.deleteAll();
```

#### `await db.deleteAllAsync()` → `boolean`

Clear all data asynchronously (non-blocking). Recommended for large datasets.

```tsx
await db.deleteAllAsync();
```

---

### Asynchronous API

> **Note**: These methods run on a background DBWorker thread, non-blocking.

#### `await db.setAsync(key, value)` → `boolean`

Async set for large values.

```tsx
await db.setAsync('largeConfig', hugeObject);
```

#### `await db.getAsync<T>(key)` → `T | undefined`

Async read for large values.

```tsx
const data = await db.getAsync('largeConfig');
```

#### `await db.setMultiAsync(entries)` → `boolean`

Async batch write — atomic, all-or-nothing.

```tsx
await db.setMultiAsync({
  item1: {...},
  item2: {...},
  item3: {...},
});
```

#### `await db.getMultipleAsync(keys)` → `Record<string, any>`

Async batch read.

```tsx
const results = await db.getMultipleAsync(['a', 'b', 'c']);
```

#### `await db.getAllKeysAsync()` → `string[]`

Async key enumeration.

```tsx
const allKeys = await db.getAllKeysAsync();
```

#### `await db.rangeQueryAsync(startKey, endKey)` → `RangeQueryResult[]`

Async range query.

```tsx
const results = await db.rangeQueryAsync('user:', 'user;\uffff');
```

---

### Advanced Features

#### TTL (Time-To-Live)

#### `db.setWithTTL(key, value, ttlMs)` → `boolean`

Set a value that expires after `ttlMs` milliseconds.

```tsx
// Expires in 1 hour
db.setWithTTL('temp_token', 'abc123', 60 * 60 * 1000);
```

#### `db.cleanupExpired()`

Manually remove expired keys.

```tsx
db.cleanupExpired();
```

#### Merging

#### `db.merge(key, partial)` → `boolean`

Deep merge into existing object.

```tsx
db.set('user', { name: 'Alice', age: 30 });
db.merge('user', { age: 31 }); // { name: 'Alice', age: 31 }
```

#### `await db.mergeAsync(key, partial)` → `boolean`

Async merge.

```tsx
await db.mergeAsync('user', { settings: { darkMode: true } });
```

#### Conditional Writes

#### `db.setIfNotExists(key, value)` → `boolean`

Set only if key doesn't exist.

```tsx
const wasSet = db.setIfNotExists('initialized', true);
```

#### `await db.setIfNotExistsAsync(key, value)` → `boolean`

Async version.

```tsx
const wasSet = await db.setIfNotExistsAsync('initialized', true);
```

#### Compare-And-Set

#### `db.compareAndSet(key, expected, next)` → `boolean`

Atomic CAS operation.

```tsx
const success = db.compareAndSet('counter', 5, 6);
```

#### `await db.compareAndSetAsync(key, expected, next)` → `boolean`

Async CAS.

```tsx
const success = await db.compareAndSetAsync('counter', 5, 6);
```

#### Pagination & Streaming

#### `async db.streamKeys()` → `AsyncGenerator<string>`

Stream keys one at a time (memory-efficient for large datasets).

```tsx
for await (const key of db.streamKeys()) {
  console.log(key);
}
```

#### Query API

#### `await db.query(options)` → `any[]`

Complex queries with filter, sort, and limit.

```tsx
const results = await db.query({
  prefix: 'user:',
  filter: (user) => user.active,
  sort: (a, b) => a.name.localeCompare(b.name),
  limit: 10,
});
```

---

### Encryption & Security

#### `db.setSecureMode(enable: boolean)`

Enable/disable encryption for all stored data.

```tsx
db.setSecureMode(true);
```

#### Hardware Secure Enclave

For extremely sensitive data (PINs, biometrics, tokens):

#### `await db.setSecureItemAsync(key, value)` → `boolean`

Store in hardware-backed keystore.

```tsx
await db.setSecureItemAsync('pin:user123', hashedPin);
```

#### `await db.getSecureItemAsync(key)` → `string | null`

Retrieve secure item.

```tsx
const pin = await db.getSecureItemAsync('pin:user123');
```

#### `await db.deleteSecureItemAsync(key)` → `boolean`

Delete secure item.

```tsx
await db.deleteSecureItemAsync('pin:user123');
```

---

### Synchronization

#### SyncManager

Built-in offline-first sync with conflict resolution.

```tsx
import { TurboDB, SyncManager } from 'react-native-turbo-db';

const db = await TurboDB.create('app');

const syncManager = new SyncManager(
  db,
  {
    pullChanges: async (lastVersion) => {
      const response = await fetch(`/api/sync?since=${lastVersion}`);
      return response.json();
    },
    pushChanges: async (changes) => {
      const response = await fetch('/api/sync', {
        method: 'POST',
        body: JSON.stringify(changes),
      });
      return response.json();
    },
  },
  {
    autoSync: true,
    syncIntervalMs: 30000,
  }
);

// Listen to sync events
syncManager.onSyncEvent((event, data) => {
  if (event === 'error') {
    console.error('Sync failed:', data);
  }
});

await syncManager.start();
```

---

### Diagnostics

#### `db.getStats()` → `DBStats`

Get database statistics.

```tsx
const stats = db.getStats();
console.log(stats.nodeCount, stats.fragmentationRatio);
```

#### `db.verifyHealth()` → `boolean`

Verify database integrity.

```tsx
const healthy = db.verifyHealth();
```

#### `db.repair()` → `boolean`

Repair database if corrupted.

```tsx
db.repair();
```

#### `db.getMetrics()` → `object`

Get detailed engine metrics.

```tsx
const metrics = db.getMetrics();
```

---

### Data Migration

#### `await db.migrate(fromVersion, toVersion, migrationFn)`

Run schema migrations.

```tsx
await db.migrate(1, 2, async (db) => {
  // Migrate from v1 to v2
  const users = await db.getAllKeysAsync();
  for (const key of users) {
    const user = await db.getAsync(key);
    await db.setAsync(key, { ...user, version: 2 });
  }
});
```

---

### Export / Import

#### `await db.export()` → `Record<string, any>`

Export all user data as a plain object.

```tsx
const data = await db.export();
```

#### `await db.import(data)`

Import data from a plain object.

```tsx
await db.import({
  user: { name: 'Alice' },
  settings: { theme: 'dark' },
});
```

---

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                        JavaScript                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────┐   │
│  │ TurboDB API │  │  SyncManager│  │  Secure Enclave API │   │
│  └──────┬──────┘  └──────┬──────┘  └──────────┬──────────┘   │
└─────────┼────────────────┼───────────────────┼───────────────┘
          │                │                    │
          ▼                ▼                    ▼
┌──────────────────────────────────────────────────────────────┐
│                    JSI (JavaScript Interface)                │
│  Direct C++ function calls - no async bridge overhead        │
└──────────────────────────────────────────────────────────────┘
          │
          ▼
┌───────────────────────────────────────────────────────────────┐
│                        C++ Engine                             │
│  ┌──────────────────────────────────────────────────────────┐ │
│  │                    DBEngine                              │ │
│  │  ┌─────────────┐  ┌──────────────┐  ┌──────────────┐     │ │
│  │  │ B+Tree Index│  │  WAL Manager │  │ CryptoContext│     │ │
│  │  └─────────────┘  └──────────────┘  └──────────────┘     │ │
│  └──────────────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────────┘
          │
          ▼
┌─────────────────────────────────────────────────────────────┐
│                      Storage Layer                          │
│  ┌────���─���───────┐    ┌────────────────────────────────┐ │
│  │  MemoryMapped    │    │  Write-Ahead Log (WAL)         │ │
│  │  File (mmap)     │    │  ACID transaction保障           │ │
│  └──────────────────┘    └────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### Performance Characteristics

| Operation    | Complexity   | Notes               |
| ------------ | ------------ | ------------------- |
| `get`        | O(log n)     | B+Tree index lookup |
| `set`        | O(log n)     | Includes encryption |
| `rangeQuery` | O(log n + k) | k = result count    |
| `getAllKeys` | O(n)         | Enumerates all keys |

## Comparison

| Feature            | TurboDB            | AsyncStorage | SQLite (bridge) | MMKV |
| ------------------ | ------------------ | ------------ | --------------- | ---- |
| Sync reads         | ✅                 | ❌           | ❌              | ✅   |
| Encryption         | XChaCha20-Poly1305 | ❌           | ❌              | ❌   |
| Hardware keystore  | ✅                 | ❌           | ❌              | ❌   |
| WAL logging        | ✅                 | ❌           | ✅              | ❌   |
| Offline-first sync | ✅                 | ❌           | ❌              | ❌   |
| SSR support        | ✅                 | ❌           | ❌              | ❌   |
| B+Tree indexing    | ✅                 | ❌           | ✅              | ❌   |

## Platform Support

| Platform                | Status      | Notes              |
| ----------------------- | ----------- | ------------------ |
| React Native (New Arch) | ✅ Full     | JSI + TurboModules |
| React Native (Old Arch) | ⚠️ Fallback | Uses bridge        |
| iOS                     | ✅ Full     | 15.1+              |
| Android                 | ✅ Full     | API 24+ (7.0)      |
| Web                     | ✅ Full     | IndexedDB          |
| SSR (Next.js/Remix)     | ✅ Full     | Server-safe        |
| Node.js                 | ✅ Full     | IndexedDB polyfill |

## Troubleshooting

### "Native module not found"

Ensure you've rebuilt after installation:

```bash
npx react-native run-ios
# or
npx react-native run-android
```

Verify New Architecture is enabled:

```tsx
// android/gradle.properties
newArchEnabled = true;
```

### Slow first launch

First run includes key generation (~256-bit AES). Subsequent launches are instantaneous.

### Data doesn't persist after update

TurboDB stores in the app's documents directory — data persists across app updates.

### TypeScript errors

Ensure `esModuleInterop` is enabled in your `tsconfig.json`:

```json
{
  "compilerOptions": {
    "esModuleInterop": true
  }
}
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup.

```bash
# Clone and install
git clone https://github.com/ganeshjayaprakash/react-native-turbo-db.git
cd react-native-turbo-db
npm install

# Build
npm run prepare

# Test
npm test

# Lint
npm run lint
```

## License

MIT © [Ganesh Jayaprakash](https://github.com/ganeshjayaprakash)

---

<p align="center">Built with performance in mind. 🚀</p>
