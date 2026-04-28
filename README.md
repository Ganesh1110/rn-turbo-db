# react-native-turbo-db

[![NPM Version](https://img.shields.io/npm/v/react-native-turbo-db.svg?style=flat-square)](https://www.npmjs.com/package/react-native-turbo-db)
[![License](https://img.shields.io/npm/l/react-native-turbo-db.svg?style=flat-square)](https://npmjs.org/package/react-native-turbo-db)
[![Platform](https://img.shields.io/badge/platform-android%20%7C%20ios%20%7C%20web-blue.svg?style=flat-square)](https://npmjs.org/package/react-native-turbo-db)
[![New Architecture](https://img.shields.io/badge/architecture-TurboModule%20%7C%20JSI-green.svg?style=flat-square)](https://reactnative.dev/docs/the-new-architecture/landing)
[![Web Support](https://img.shields.io/badge/web-IndexedDB-orange.svg?style=flat-square)](https://developer.mozilla.org/en-US/docs/Web/API/IndexedDB_API)

## 🏗️ Supported Platforms & Frameworks

| Platform                            | Support    | Description                               |
| :---------------------------------- | :--------- | :---------------------------------------- |
| **React Native (New Architecture)** | ✅ Full    | TurboModule + JSI for maximum performance |
| **React Native (Old Architecture)** | ⚠️ Limited | Works but uses fallback bridge            |
| **React JS / Vanilla JS**           | ✅ Full    | Uses IndexedDB backend, SSR-safe          |
| **Node.js / Deno**                  | ✅ Full    | Uses IndexedDB polyfill or localStorage   |
| **Next.js (SSR)**                   | ✅ Full    | Server-side rendering safe                |
| **Remix**                           | ✅ Full    | Server-side rendering safe                |
| **Expo**                            | ✅ Full    | Works with `npx expo prebuild`            |
| **React Native Web**                | ✅ Full    | Uses IndexedDB fallback                   |

> **Note**: TurboDB is designed for React Native's New Architecture (TurboModules). For web/SSR platforms, it automatically falls back to IndexedDB with the same API.

## 🌟 Why TurboDB?

Most React Native storage solutions rely on the asynchronous bridge (AsyncStorage) or complex SQLite wrappers. **TurboDB** is built for the **New Architecture**, using **JSI (JavaScript Interface)** to expose a native C++ engine directly to the JavaScript runtime.

- **Zero Serialization**: No more JSON.stringify overhead on every write.
- **Instant Reads**: Access data synchronously in your render functions without "Flash of Missing Content".
- **Military Grade**: Data is encrypted using **XChaCha20-Poly1305 AEAD** (Libsodium) before it ever touches the disk.
- **SEO & SSR Optimized**: Built-in isomorphic support with a robust `IndexedDB` backend for web, enabling synchronous hydration and zero-CLS (Cumulative Layout Shift).

## 🚀 Features

- **Turbo Module**: 100% compatible with React Native's New Architecture.
- **Hardware-Backed Keys**: Master keys are protected by the Android KeyStore and iOS Keychain.
- **ACID Compliant (WAL)**: Write-Ahead Logging ensures your data survives app crashes or power loss.
- **Smart Memory Mapping**: Uses `mmap` for efficient I/O, allowing the OS to handle caching optimally.
- **Sync & Async APIs**: Use `.get()` for instant results or `.getAsync()` for heavy background processing.
- **Rich Querying**: Built-in support for lexicographical range queries, batch operations, and **paginated key retrieval**.

## 📦 Installation

```sh
npm install react-native-turbo-db
# or
yarn add react-native-turbo-db
```

### Requirements

| Requirement               | Version           | Notes                            |
| :------------------------ | :---------------- | :------------------------------- |
| **React Native**          | ≥0.76.0           | Recommended for New Architecture |
| **React Native CLI**      | Latest            | Required for native builds       |
| **Node.js**               | ≥18.0.0           | For build tooling                |
| **iOS Deployment Target** | ≥15.1             | Required for TurboModules        |
| **Android minSdkVersion** | ≥24 (Android 7.0) | For Libsodium crypto support     |

### iOS

```sh
cd ios && pod install
```

### Android

No additional setup required! Ensure you have the New Architecture enabled in your `gradle.properties`.

### Web (SSR/Next.js/Remix)

TurboDB works out-of-the-box on the web using `IndexedDB`. It is SSR-safe and won't crash during server-side rendering.

### Vanilla JavaScript / Node.js

You can also use TurboDB in plain JavaScript projects:

```html
<script type="module">
  import { TurboDB } from 'https://esm.sh/react-native-turbo-db';

  const db = new TurboDB('my_data', 10 * 1024 * 1024);
  db.set('hello', 'world');
  console.log(db.get('hello')); // "world"
</script>
```

For Node.js, install and use:

```bash
npm install react-native-turbo-db
```

```javascript
import { TurboDB } from 'react-native-turbo-db';

const db = new TurboDB('./data.db');
db.set('key', 'value');
```

## 🛠 Usage

### Initialization

Initialize the database. It's recommended to do this once at the app root.

```typescript
import { TurboDB } from 'react-native-turbo-db';

// Get a platform-specific safe path
const docsDir = TurboDB.getDocumentsDirectory();
const dbPath = `${docsDir}/app_secure_v1.db`;

// Create DB instance (Default: 10MB file, automatically expands)
const db = new TurboDB(dbPath, 10 * 1024 * 1024);
```

### Basic CRUD (Synchronous)

```typescript
// CREATE / UPDATE
db.set('user', { id: '7', role: 'admin', settings: { theme: 'dark' } });

// READ
const user = db.get<{ id: string }>('user');

// DELETE
db.remove('user');

// CHECK EXISTENCE
const exists = db.has('user');
```

### Pagination & Range Queries

```typescript
// Paged Key Retrieval (Performance optimization for large datasets)
const first100Keys = db.getAllKeysPaged(100, 0);

// Multi-Set (Atomic)
db.setMulti({
  token: 'secret_abc',
  expires: 3600,
});

// Range Query (Great for paginated lists or time-series data)
const logs = db.rangeQuery('log_2023-01-01', 'log_2023-12-31');
```

### Asynchronous Operations

Avoid blocking the UI thread for extremely large payloads:

```typescript
async function heavyWork() {
  const data = await db.getAsync('massive_config_file');
  await db.setAsync('background_task_result', { status: 'done' });
}
```

## 🔐 Security

TurboDB generates a unique 256-bit master key for every installation.

- **Android**: Stored in encrypted `SharedPreferences`.
- **iOS**: Stored in `Keychain` with `kSecAttrAccessibleAfterFirstUnlock`.
- **Encryption**: Records are encrypted using `crypto_aead_xchacha20poly1305_ietf`, providing both confidentiality and authenticity (MAC).

## 📊 Performance Benchmarks

_Tested on iPhone 15 Pro / Pixel 8. Operations per 1000 items._

| Operation       | TurboDB (JSI) | AsyncStorage | SQLite (Bridge) |
| :-------------- | :------------ | :----------- | :-------------- |
| **Bulk Write**  | **~10ms**     | ~180ms       | ~60ms           |
| **Random Read** | **~4ms**      | ~120ms       | ~45ms           |

## 📋 API Reference

| Method                              | Description           | Platform Support |
| :---------------------------------- | :-------------------- | :--------------- |
| `db.set(key, value)`                | Synchronous write     | Native, Web      |
| `db.get(key)`                       | Synchronous read      | Native, Web      |
| `db.has(key)`                       | Check key existence   | Native, Web      |
| `db.remove(key)` / `db.del(key)`    | Delete a key          | Native, Web      |
| `db.setMulti(entries)`              | Bulk atomic write     | Native, Web      |
| `db.getMultiple(keys)`              | Bulk read             | Native, Web      |
| `db.getAllKeys()`                   | Get all keys          | Native, Web      |
| `db.getAllKeysPaged(limit, offset)` | Paginated keys        | Native only      |
| `db.rangeQuery(startKey, endKey)`   | Range query           | Native only      |
| `db.clear()` / `db.deleteAll()`     | Clear all data        | Native, Web      |
| `db.flush()`                        | Force write to disk   | Native only      |
| `db.benchmark()`                    | Performance benchmark | Native only      |
| `TurboDB.install()`                 | Initialize JSI        | Native only      |
| `TurboDB.getDocumentsDirectory()`   | Get data directory    | Native, Web      |

## 🔧 Troubleshooting

### Common Issues

**"Native module 'TurboDB' not found"**

- Ensure you've rebuilt the native app: `npx react-native run-ios` or `npx react-native run-android`
- Verify New Architecture is enabled in `gradle.properties` (Android) or `Podfile` (iOS)

**Slow performance on first launch**

- First run includes key generation and initialization. Subsequent launches are faster.

**Data not persisting after app update**

- TurboDB stores data in app's documents directory. Data persists across updates.

## 🤝 Contributing

Contributions are welcome! Please see our [Contributing Guide](CONTRIBUTING.md).

## 📄 License

MIT © [Ganesh Jayaprakash](https://github.com/ganeshjayaprakash)

---

Built with performance in mind. Happy coding! 🚀
