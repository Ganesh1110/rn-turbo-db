# TurboDB

<p align="center">
  <a href="https://www.npmjs.com/package/react-native-turbo-db"><img src="https://img.shields.io/npm/v/react-native-turbo-db.svg" alt="npm version" /></a>
  <a href="https://www.npmjs.com/package/react-native-turbo-db"><img src="https://img.shields.io/npm/dm/react-native-turbo-db.svg" alt="npm downloads" /></a>
  <a href="https://github.com/ganeshjayaprakash/react-native-turbo-db/blob/main/LICENSE"><img src="https://img.shields.io/npm/l/react-native-turbo-db.svg" alt="MIT License" /></a>
  <a href="https://reactnative.dev/"><img src="https://img.shields.io/badge/React%20Native-0.85-blue.svg" alt="React Native" /></a>
</p>

A **high-performance, encrypted embedded database** for React Native built on C++. Leverages the New Architecture (JSI) for direct, native-speed synchronous operations without bridge serialization overhead.

---

## ⚡ What's New in v1.2.0 (Performance Engine)

TurboDB v1.2.0 brings a massive performance jump to your data layer:
- **Data-Level LRU Cache** — Frequently accessed keys are served directly from an in-memory JSI cache, bypassing `mmap` and deserialization entirely.
- **Zero-Copy Path** — Simple data types (strings, numbers) are read directly from memory views with zero overhead.
- **Read-Ahead Prefetching** — Drastically speeds up `rangeQuery` and `getByPrefix` operations.
- **Built-in Compression** — Seamless `zlib` integration for large objects, saving up to 60% of disk space.

*(Built on top of v1.1.0's Core Reliability Engine: Atomic Transactions, 10x WAL Batching, and B+Tree Auto-Repair).*

---

## 🚀 Quick Start

### Installation
```bash
npm install react-native-turbo-db
# Ensure newArchEnabled=true in android/gradle.properties
# cd ios && pod install
```

### Basic Usage
```tsx
import { TurboDB } from 'react-native-turbo-db';

// Initialize (10MB initial size, WAL enabled)
const db = await TurboDB.create('my_app', 10 * 1024 * 1024, { syncEnabled: true });

// Synchronous (instant)
db.set('user:1', { name: 'Alice', role: 'admin' });
const user = db.get('user:1');

// Asynchronous (offloads to background DBWorker thread)
await db.setAsync('largeData', hugePayload);
const data = await db.getAsync('largeData');
```

---

<details>
<summary>📚 <strong>Full API Reference (Click to Expand)</strong></summary>

### Initialization
| Method | Description |
|--------|-------------|
| `TurboDB.create(path, size, options)` | Async factory. Returns `Promise<TurboDB>`. |
| `TurboDB.install()` | Installs native JSI bindings (called automatically). |

### Synchronous (Blocking, Fast Path)
| Method | Description |
|--------|-------------|
| `get(key)` | Returns parsed object/primitive or `undefined`. |
| `set(key, val)` | Writes to storage. Returns `boolean`. |
| `has(key)` | Checks if key exists. |
| `remove(key)` | Deletes a record. |
| `setMulti(obj)` | Atomic batch insert. |
| `getMultiple(keys)` | Batch retrieval. |
| `rangeQuery(start, end)` | Lexicographical range fetch. |
| `getByPrefix(prefix)` | Fetch all keys starting with `prefix`. |
| `deleteAll()` | Synchronous database wipe. |

### Asynchronous (Non-blocking DBWorker Thread)
| Method | Description |
|--------|-------------|
| `getAsync(key)` | Background read. Returns `Promise<any>`. |
| `setAsync(key, val)` | Background write. Returns `Promise<boolean>`. |
| `setMultiAsync(obj)` | **10x Faster WAL Batching**. Atomic background batch write. |
| `removeAsync(key)` | Background delete. |
| `deleteAllAsync()` | **Fast-path wipe**, recommended for large datasets. |
| `rangeQueryAsync(start, end)` | Background range fetch. |

### Advanced
- **TTL**: `setWithTTL(key, value, ttlMs)` and `cleanupExpired()`
- **Compare & Set**: `compareAndSet(key, expected, next)`
- **Merge**: `merge(key, partialObj)`
- **Hardware Enclave (Secure Mode)**: `setSecureItemAsync(key, val)`, `getSecureItemAsync(key)`
- **Streaming**: `for await (const key of db.streamKeys()) { ... }`

</details>

<details>
<summary>🛠️ <strong>Architecture & Sync (Click to Expand)</strong></summary>

### Architecture Overview
TurboDB uses a custom **B+Tree Index** sitting on top of a **Memory-Mapped (mmap)** file, protected by a **Write-Ahead Log (WAL)** for ACID compliance. The entire stack communicates directly with JavaScript via **JSI**, entirely skipping the React Native JSON bridge.

### SyncManager (Offline-First)
Built-in synchronization engine for remote backends:
```tsx
import { SyncManager } from 'react-native-turbo-db';

const syncManager = new SyncManager(db, {
  pullChanges: async (lastClock) => fetch(`/api/sync?since=${lastClock}`).then(r => r.json()),
  pushChanges: async (changes) => fetch('/api/sync', { method: 'POST', body: JSON.stringify(changes) }).then(r => r.json()),
}, { autoSync: true });

await syncManager.start();
```
</details>

---

## 🆚 Why TurboDB?

| Feature | TurboDB | AsyncStorage | SQLite (Bridge) | MMKV |
|---------|---------|--------------|-----------------|------|
| **Architecture** | **JSI C++** | Async Bridge | Async Bridge | JSI C++ |
| **Encryption** | **XChaCha20** | ❌ | ❌ | ❌ |
| **Transactions (WAL)** | ✅ | ❌ | ✅ | ❌ |
| **Data Types** | **JSON / Objects**| Strings only | Strings/Rows | Primitives |
| **Indexing** | **B+Tree** | ❌ | ✅ | ❌ |

## 🌐 Platform Support
- **React Native (New Arch)**: Full Support (iOS 15.1+, Android 7.0+)
- **React Native (Old Arch)**: Fallback Support
- **Web / SSR**: Full Support (IndexedDB backed)

## 📄 License
MIT © [Ganesh Jayaprakash](https://github.com/ganeshjayaprakash)
