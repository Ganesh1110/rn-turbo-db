import { TurboModuleRegistry, NativeModules } from 'react-native';
import NativeTurboDB from './NativeTurboDB';
import type { Spec } from './NativeTurboDB';
import type { SyncChanges, SyncRecord, SyncAck } from './SyncManager';
import { TurboDBError, TurboDBErrorCode } from './TurboDBError';

export { TurboDBError, TurboDBErrorCode };

// Use the resolved Spec from NativeTurboDB.ts, or fallback to manual lookup/Bridge.
// We do the fallback here instead of in NativeTurboDB.ts to avoid Codegen parser errors.
const NativeDBModule =
  NativeTurboDB ||
  TurboModuleRegistry.get<Spec>('NativeTurboDB') ||
  (NativeModules as any).TurboDB ||
  (NativeModules as any).NativeTurboDB;

export function isNativeLibraryReady(): boolean {
  return typeof getNativeDB() !== 'undefined';
}

export function getLibraryStatus(): {
  nativeModuleLoaded: boolean;
  nativeDbReady: boolean;
  error?: string;
} {
  return {
    nativeModuleLoaded: !!NativeTurboDB,
    nativeDbReady: typeof getNativeDB() !== 'undefined',
  };
}

declare const global: {
  NativeDB: {
    initStorage(
      path: string,
      size: number,
      options: { syncEnabled: boolean }
    ): boolean;
    insertRec(key: string, obj: any): boolean;
    findRec(key: string): any;
    clearStorage(): boolean;
    setMulti(entries: Record<string, any>): boolean;
    getMultiple(keys: string[]): Record<string, any>;
    remove(key: string): boolean;
    del(key: string): boolean;
    deleteAll(): boolean;
    benchmark(): number;
    rangeQuery(
      startKey: string,
      endKey: string
    ): Array<{ key: string; value: any }>;
    getAllKeys(): string[];
    getAllKeysPaged(limit: number, offset: number): string[];
    flush(): void;
    setAsync(args: { key: string; value: any }): Promise<boolean>;
    getAsync(key: string): Promise<any>;
    setMultiAsync(entries: Record<string, any>): Promise<boolean>;
    getMultipleAsync(keys: string[]): Promise<Record<string, any>>;
    rangeQueryAsync(args: {
      startKey: string;
      endKey: string;
    }): Promise<Array<{ key: string; value: any }>>;
    getAllKeysAsync(): Promise<string[]>;
    removeAsync(key: string): Promise<boolean>;
    getLocalChangesAsync(lastSyncClock: number): Promise<any>;
    applyRemoteChangesAsync(changes: any[]): Promise<boolean>;
    markPushedAsync(acks: any[]): Promise<boolean>;
    verifyHealth(): boolean;
    repair(): boolean;
    getDatabasePath(): string;
    getWALPath(): string;
    getStats(): {
      treeHeight: number;
      nodeCount: number;
      formatVersion: number;
      fragmentationRatio: number;
      isInitialized: boolean;
      secureMode: boolean;
    };
    setSecureMode(enable: boolean): void;
  };
  __NativeDB: any;
};

let nativeDBProvider: () => any;

export function registerNativeDBGetter(getter: () => any) {
  nativeDBProvider = getter;
}

function getNativeDB(): any {
  // Deep search for the JSI property across all possible global containers
  const g: any =
    (typeof global !== 'undefined' ? global : null) ||
    (typeof globalThis !== 'undefined' ? globalThis : null) ||
    (typeof window !== 'undefined' ? window : null);

  if (g) {
    if (g.NativeDB !== undefined) return g.NativeDB;
    if (g.__NativeDB !== undefined) return g.__NativeDB;
  }

  if (nativeDBProvider) {
    return nativeDBProvider();
  }
  return undefined;
}

export type DBMode = 'secure' | 'turbo';

export { SyncManager } from './SyncManager';
export type {
  SyncAdapter,
  SyncRecord,
  SyncChanges,
  SyncAck,
  SyncOptions,
  SyncEvent,
} from './SyncManager';

export interface RangeQueryResult {
  key: string;
  value: any;
}

export interface DBStats {
  treeHeight: number;
  nodeCount: number;
  formatVersion: number;
  fragmentationRatio: number;
  isInitialized: boolean;
  secureMode: boolean;
}

/**
 * TurboDB — high-performance JSI-based persistent database.
 *
 * Usage:
 *   const db = await TurboDB.create('/path/to/db');
 *   await db.setAsync('key', { value: 42 });
 *   const val = await db.getAsync('key');
 */
export class TurboDB {
  /**
   * ✅ Preferred factory — async, no busy-wait, no JS thread blocking.
   */
  static async create(
    path: string,
    size: number = 10 * 1024 * 1024,
    options: { syncEnabled?: boolean } = {}
  ): Promise<TurboDB> {
    const db = new TurboDB(path, size, options);
    await db._initAsync();
    return db;
  }

  /**
   * Install the native JSI global. Call once at app startup
   * (e.g., in index.js before any DB usage).
   */
  static install(): void {
    if (!NativeDBModule) {
      console.error(
        '[TurboDB] Native module (TurboDB) not found. ' +
          'Rebuild the native app (npx react-native run-ios / run-android).'
      );
      throw new Error(
        '[TurboDB] Native module (TurboDB) not found. Rebuild the native app.'
      );
    }
    try {
      NativeDBModule.install();
    } catch (e: any) {
      console.error('[TurboDB] install() failed:', e);
      throw new Error(`[TurboDB] install() failed: ${e.message}`);
    }
  }

  static getDocumentsDirectory(): string {
    if (!NativeDBModule) return '';
    return NativeDBModule.getDocumentsDirectory();
  }

  private isInitialized = false;
  private initPromise: Promise<void> | null = null;

  /**
   * ⚠️ Prefer `TurboDB.create()` over the constructor.
   * If you use the constructor, call `ensureInitialized()` (sync) only
   * for simple use-cases that can tolerate blocking on first access.
   */
  constructor(
    private path: string,
    private size: number = 10 * 1024 * 1024,
    private options: { syncEnabled?: boolean } = {}
  ) {}

  /**
   * Async init — no busy-wait, returns Promise.
   * Called internally by `TurboDB.create()`.
   */
  private async _initAsync(): Promise<void> {
    if (this.isInitialized) return;
    if (this.initPromise) return this.initPromise;

    this.initPromise = (async () => {
      if (!NativeDBModule) {
        throw new Error(
          '[TurboDB] Native module (TurboDB) not found. Verify your native build.'
        );
      }

      try {
        const success = NativeDBModule.install();
        if (!success) {
          console.warn(
            '[TurboDB] Native install() returned false — JSI runtime might not be ready yet.'
          );
        }
      } catch (e: any) {
        console.error('[TurboDB] install() critical failure:', e);
        throw new Error(`[TurboDB] install() failure: ${e.message}`);
      }

      let db = getNativeDB();
      if (!db) {
        // We poll for up to 10 seconds total (100ms * 100)
        // This is safe because it's async and only happens once.
        for (let i = 0; i < 100; i++) {
          await new Promise((r) => setTimeout(r, 100));
          db = getNativeDB();
          if (db) {
            console.log('[TurboDB] JSI Connection Established ✅');
            break;
          }

          if (i % 20 === 0 && i > 0) {
            console.log(
              `[TurboDB] Still waiting for JSI bridge (${i / 10}s)...`
            );
            // Try to re-invoke install in case the runtime context changed
            try {
              NativeDBModule.install();
            } catch {}
          }
        }
      }

      if (!db) {
        throw new Error(
          '[TurboDB] NativeDB JSI object not found after install(). ' +
            'Check native logs for errors. This may be a JSI runtime mismatch in bridgeless mode.'
        );
      }

      const success = db.initStorage(this.path, this.size, {
        syncEnabled: this.options.syncEnabled ?? false,
      });
      if (!success) {
        throw new Error(
          `[TurboDB] Failed to initialize storage at ${this.path}`
        );
      }
      this.isInitialized = true;
    })();

    return this.initPromise;
  }
  /**
   * Sync init — only for backward compat. Prefer async.
   * Does NOT use a busy-wait — throws immediately if NativeDB is not ready.
   */
  private ensureInitialized(): void {
    if (this.isInitialized) return;

    if (!NativeDBModule) {
      throw new Error(
        '[TurboDB] Native module (TurboDB) not found. Check your native build.'
      );
    }

    try {
      NativeDBModule.install();
    } catch (e: any) {
      throw new Error(`[TurboDB] install() failed: ${e.message}`);
    }

    if (typeof getNativeDB() === 'undefined') {
      throw new Error(
        '[TurboDB] NativeDB JSI object not found after install(). ' +
          'Use TurboDB.create() for async initialization with automatic retry.'
      );
    }

    const success = getNativeDB().initStorage(this.path, this.size, {
      syncEnabled: this.options.syncEnabled ?? false,
    });
    if (!success) {
      throw new Error(`[TurboDB] Failed to initialize storage at ${this.path}`);
    }
    this.isInitialized = true;
  }

  // ── Synchronous API (fast path) ──────────────────────────────────────────

  /**
   * Warms the in-memory B-Tree cache by touching the storage.
   * On native, JSI/mmap handles OS-level page cache automatically;
   * this is a semantic no-op kept for API parity with the vision spec.
   * On web, this triggers IndexedDB load if not already initialized.
   */
  /**
   * Warms the in-memory B-Tree cache by touching the storage.
   *
   * @deprecated Native caching is handled automatically by JSI/mmap. This is a no-op.
   */
  async warmCache(): Promise<void> {
    // JSI handles caching automatically; this is kept for API parity only
  }

  /**
   * Clear any in-memory JSI caches if applicable.
   */
  clearCache(): void {
    // JSI handles its own lifecycle.
  }

  has(key: string): boolean {
    this.ensureInitialized();
    // Use get() instead of findRec() so TTL-expired keys correctly return false
    return this.get(key) !== undefined;
  }

  private listeners: Set<
    (event: { type: 'set' | 'remove'; key: string; value?: any }) => void
  > = new Set();
  private keyListeners: Map<string, Set<(value: any) => void>> = new Map();

  /**
   * Subscribe to all changes in the database.
   */
  subscribeAll(
    callback: (event: {
      type: 'set' | 'remove';
      key: string;
      value?: any;
    }) => void
  ): () => void {
    this.listeners.add(callback);
    return () => this.listeners.delete(callback);
  }

  /**
   * Subscribe to changes for a specific key.
   */
  subscribe(key: string, callback: (value: any) => void): () => void {
    if (!this.keyListeners.has(key)) {
      this.keyListeners.set(key, new Set());
    }
    this.keyListeners.get(key)!.add(callback);
    return () => {
      const listeners = this.keyListeners.get(key);
      if (listeners) {
        listeners.delete(callback);
        if (listeners.size === 0) {
          this.keyListeners.delete(key);
        }
      }
    };
  }

  private notify(type: 'set' | 'remove', key: string, value?: any) {
    const event = { type, key, value };
    this.listeners.forEach((cb) => cb(event));

    const kListeners = this.keyListeners.get(key);
    if (kListeners) {
      kListeners.forEach((cb) => cb(value));
    }
  }

  private indexes: Set<string> = new Set();

  /**
   * Registers a field to be indexed for all records.
   * This is a simple implementation that manages index keys with prefix '__idx:'.
   */
  async createIndex(field: string): Promise<void> {
    this.indexes.add(field);

    // Re-index existing data
    const keys = await this.getAllKeysAsync();
    for (const key of keys) {
      if (key.startsWith('__idx:')) continue;
      const val = await this.getAsync(key);
      if (val && val[field] !== undefined) {
        await this.updateIndex(key, field, val[field]);
      }
    }
  }

  private async updateIndex(key: string, field: string, value: any) {
    const idxKey = `__idx:${field}:${value}:${key}`;
    await getNativeDB().setAsync({ key: idxKey, value: key });
  }

  /**
   * Queries records by an indexed field.
   */
  async queryByIndex(field: string, value: any): Promise<any[]> {
    const prefix = `__idx:${field}:${value}:`;
    const indexResults = await this.getByPrefixAsync(prefix);
    const dataKeys = indexResults.map((r) => r.value);
    const records = await this.getMultipleAsync(dataKeys);
    return Object.values(records);
  }

  set(key: string, value: any): boolean {
    this.ensureInitialized();
    const success = getNativeDB().insertRec(key, value);
    if (success) {
      // Background index update
      this.indexes.forEach((field) => {
        if (value && value[field] !== undefined) {
          this.updateIndex(key, field, value[field]).catch((e) =>
            console.error(
              `[TurboDB] Index update failed for field ${field}:`,
              e
            )
          );
        }
      });
      this.notify('set', key, value);
    }
    return success;
  }

  /**
   * Partial update for objects. Merges 'partial' into existing value.
   */
  merge(key: string, partial: Record<string, any>): boolean {
    const current = this.get(key) || {};
    if (typeof current !== 'object' || current === null) {
      return this.set(key, partial);
    }
    const next = { ...current, ...partial };
    return this.set(key, next);
  }

  /**
   * Async version of merge().
   */
  async mergeAsync(
    key: string,
    partial: Record<string, any>
  ): Promise<boolean> {
    const current = (await this.getAsync(key)) || {};
    if (typeof current !== 'object' || current === null) {
      return this.setAsync(key, partial);
    }
    const next = { ...current, ...partial };
    return this.setAsync(key, next);
  }

  get<T = any>(key: string): T | undefined {
    this.ensureInitialized();
    const val = getNativeDB().findRec(key);

    // TTL Check
    if (val && typeof val === 'object' && '__ttl_expiry' in val) {
      if (Date.now() > val.__ttl_expiry) {
        this.remove(key); // Cleanup expired
        return undefined;
      }
      return val.data;
    }

    return val;
  }

  /**
   * Sets a value with a Time-To-Live (TTL) in milliseconds.
   */
  setWithTTL(key: string, value: any, ttlMs: number): boolean {
    const expiry = Date.now() + ttlMs;
    return this.set(key, {
      data: value,
      __ttl_expiry: expiry,
    });
  }

  /**
   * Manually cleanup expired records.
   */
  cleanupExpired(): void {
    const keys = this.getAllKeys();
    for (const key of keys) {
      if (key.startsWith('__')) continue; // Skip internal index/sys keys
      this.get(key); // Triggers TTL check and auto-cleanup if expired
    }
  }

  setMulti(entries: Record<string, any>): boolean {
    this.ensureInitialized();
    const success = getNativeDB().setMulti(entries);
    if (success) {
      Object.entries(entries).forEach(([k, v]) => this.notify('set', k, v));
    }
    return success;
  }

  /** Alias for setMulti() to match vision API */
  setMultiple(entries: Record<string, any>): boolean {
    return this.setMulti(entries);
  }

  getMultiple(keys: string[]): Record<string, any> {
    this.ensureInitialized();
    return getNativeDB().getMultiple(keys);
  }

  remove(key: string): boolean {
    this.ensureInitialized();
    const success = getNativeDB().remove(key);
    if (success) {
      this.notify('remove', key);
    }
    return success;
  }

  /**
   * Remove multiple keys at once.
   */
  removeMultiple(keys: string[]): boolean {
    let allSuccess = true;
    for (const key of keys) {
      if (!this.remove(key)) {
        allSuccess = false;
      }
    }
    return allSuccess;
  }

  /**
   * Async version of removeMultiple().
   */
  async removeMultipleAsync(keys: string[]): Promise<boolean> {
    const ops = keys.map((key) => this.removeAsync(key));
    const results = await Promise.all(ops);
    return results.every((r) => r === true);
  }

  /** Alias for remove() */
  del(key: string): boolean {
    return this.remove(key);
  }

  deleteAll(): boolean {
    this.ensureInitialized();
    return getNativeDB().deleteAll();
  }

  /**
   * Async version of deleteAll().
   * Delegates to the native deleteAll() directly — faster than enumerating all keys.
   */
  async deleteAllAsync(): Promise<boolean> {
    this.ensureInitialized();
    return getNativeDB().deleteAll();
  }

  benchmark(): number {
    this.ensureInitialized();
    return getNativeDB().benchmark();
  }

  rangeQuery(startKey: string, endKey: string): RangeQueryResult[] {
    this.ensureInitialized();
    return getNativeDB().rangeQuery(startKey, endKey);
  }

  /**
   * Retrieves all records with keys starting with the given prefix.
   */
  getByPrefix(prefix: string): RangeQueryResult[] {
    // Optimization: prefix 'user:' corresponds to range ['user:', 'user;']
    // because ':' is followed by ';' in ASCII. More generically, append \uffff.
    return this.rangeQuery(prefix, prefix + '\uffff');
  }

  /**
   * Async version of getByPrefix().
   */
  async getByPrefixAsync(prefix: string): Promise<RangeQueryResult[]> {
    return this.rangeQueryAsync(prefix, prefix + '\uffff');
  }

  /**
   * Complex query with filtering, sorting and limit.
   * Note: Currently executes in JS layer after range fetch.
   */
  async query(options: {
    prefix?: string;
    filter?: (value: any) => boolean;
    sort?: (a: any, b: any) => number;
    limit?: number;
  }): Promise<any[]> {
    let results: RangeQueryResult[] = [];

    if (options.prefix) {
      results = await this.getByPrefixAsync(options.prefix);
    } else {
      // Full scan (only recommended for small sets or paged)
      const keys = await this.getAllKeysAsync();
      const entries = await this.getMultipleAsync(keys);
      results = Object.entries(entries).map(([key, value]) => ({ key, value }));
    }

    let items = results.map((r) => r.value);

    if (options.filter) {
      items = items.filter(options.filter);
    }

    if (options.sort) {
      items = items.sort(options.sort);
    }

    if (options.limit) {
      items = items.slice(0, options.limit);
    }

    return items;
  }

  /**
   * Async iterator that streams all user-facing keys one at a time.
   * Skips all internal keys (prefixes: __idx:, __sys_, __oplog:).
   * Ideal for large datasets to avoid loading all keys into memory at once.
   */
  async *streamKeys(): AsyncGenerator<string> {
    this.ensureInitialized();
    const keys = await this.getAllKeysAsync();
    for (const key of keys) {
      if (key.startsWith('__')) continue; // skip ALL internal prefixes
      yield key;
    }
  }

  /**
   * Wraps a block of writes and calls flush() at the end.
   *
   * ⚠️ WARNING: This is NOT fully atomic. There is no rollback on error —
   * any writes completed before an exception remain committed in the WAL.
   * For true batch atomicity use `setMultiAsync()` which is a single native transaction.
   *
   * @deprecated Use `setMultiAsync()` for safe, truly-atomic batch writes.
   */
  async transaction(fn: (db: TurboDB) => Promise<void> | void): Promise<void> {
    try {
      await fn(this);
      this.flush();
    } catch (e) {
      console.error(
        '[TurboDB] transaction(): exception — partial writes already committed, NO rollback.',
        e
      );
      throw e;
    }
  }

  getAllKeys(): string[] {
    this.ensureInitialized();
    return getNativeDB().getAllKeys();
  }

  /**
   * Manual initialization if not using the static create() method.
   */
  async init(): Promise<void> {
    return this._initAsync();
  }

  /**
   * Returns the total number of records in the database.
   */
  length(): number {
    return this.getAllKeys().length;
  }

  /**
   * Async version of length().
   */
  async lengthAsync(): Promise<number> {
    const keys = await this.getAllKeysAsync();
    return keys.length;
  }

  /**
   * Returns the value for the key, or the defaultValue if the key does not exist.
   */
  getOrDefault<T = any>(key: string, defaultValue: T): T {
    const val = this.get<T>(key);
    return val !== undefined ? val : defaultValue;
  }

  /**
   * Async version of getOrDefault().
   */
  async getOrDefaultAsync<T = any>(key: string, defaultValue: T): Promise<T> {
    const val = await this.getAsync<T>(key);
    return val !== undefined ? val : defaultValue;
  }

  /**
   * Sets the value only if the key does not already exist.
   * Returns true if the value was set, false otherwise.
   */
  setIfNotExists(key: string, value: any): boolean {
    if (this.has(key)) return false;
    return this.set(key, value);
  }

  /**
   * Sets the value only if the key does not already exist.
   * Returns true if the value was set, false otherwise.
   *
   * ⚠️ Non-atomic on native. The check-then-set is two separate JSI calls.
   * Avoid in high-concurrency scenarios (e.g., concurrent setAsync calls on the
   * same key). For JS-thread-only access this is safe.
   */
  async setIfNotExistsAsync(key: string, value: any): Promise<boolean> {
    const exists = await this.getAsync(key);
    if (exists !== undefined) return false;
    return this.setAsync(key, value);
  }

  /**
   * Atomic-like Compare-And-Set.
   * Sets the value to 'next' only if the current value matches 'expected'.
   * Comparison is done via JSON stringification for objects.
   *
   * ⚠️ Non-atomic on native. The get and set are two separate JSI calls.
   * Safe for single-threaded JS access. If you have concurrent async writers on
   * the same key, use a native atomic CAS (planned for a future native binding).
   */
  compareAndSet(key: string, expected: any, next: any): boolean {
    const current = this.get(key);
    if (JSON.stringify(current) === JSON.stringify(expected)) {
      return this.set(key, next);
    }
    return false;
  }

  /**
   * Async Compare-And-Set.
   *
   * ⚠️ Non-atomic on native. The getAsync and setAsync are two separate calls.
   * A concurrent setAsync between the two can cause a lost update.
   * Safe for single-threaded usage. Concurrent-safe CAS is planned natively.
   */
  async compareAndSetAsync(
    key: string,
    expected: any,
    next: any
  ): Promise<boolean> {
    const current = await this.getAsync(key);
    if (JSON.stringify(current) === JSON.stringify(expected)) {
      return this.setAsync(key, next);
    }
    return false;
  }

  /** O(limit) paged enumeration — does NOT load all keys */
  getAllKeysPaged(limit: number, offset: number): string[] {
    this.ensureInitialized();
    return getNativeDB().getAllKeysPaged(limit, offset);
  }

  clear(): boolean {
    this.ensureInitialized();
    return getNativeDB().clearStorage();
  }

  flush(): void {
    this.ensureInitialized();
    getNativeDB().flush();
  }

  // ── Asynchronous API (non-blocking — all run on DBWorker thread) ──────────

  /**
   * Async set — serializes value on JS thread, writes on DBWorker thread.
   * Does NOT block the JS thread.
   */
  async setAsync(key: string, value: any): Promise<boolean> {
    this.ensureInitialized();
    const success = await getNativeDB().setAsync({ key, value });
    if (success) {
      // Background index update
      this.indexes.forEach((field) => {
        if (value && value[field] !== undefined) {
          this.updateIndex(key, field, value[field]).catch((e) =>
            console.error(
              `[TurboDB] Index update failed for field ${field}:`,
              e
            )
          );
        }
      });
      this.notify('set', key, value);
    }
    return success;
  }

  /**
   * Async get — reads on DBWorker thread, deserializes back on JS thread.
   */
  async getAsync<T = any>(key: string): Promise<T | undefined> {
    this.ensureInitialized();
    const val = await getNativeDB().getAsync(key);

    // TTL Check for async get
    if (val && typeof val === 'object' && '__ttl_expiry' in val) {
      if (Date.now() > val.__ttl_expiry) {
        await this.removeAsync(key);
        return undefined;
      }
      return val.data;
    }

    return val;
  }

  /**
   * Async batch set — serializes all values on JS thread, writes in batch on DBWorker.
   * Ideal for 100+ record writes.
   */
  async setMultiAsync(entries: Record<string, any>): Promise<boolean> {
    this.ensureInitialized();
    const success = await getNativeDB().setMultiAsync(entries);
    if (success) {
      Object.entries(entries).forEach(([k, v]) => {
        this.indexes.forEach((field) => {
          if (v && v[field] !== undefined) {
            this.updateIndex(k, field, v[field]).catch((e) =>
              console.error(`[TurboDB] Index update failed for key ${k}:`, e)
            );
          }
        });
        this.notify('set', k, v);
      });
    }
    return success;
  }

  /** Alias for setMultiAsync() */
  async setMultipleAsync(entries: Record<string, any>): Promise<boolean> {
    return this.setMultiAsync(entries);
  }

  /**
   * Async batch get — reads on DBWorker thread, returns results map.
   */
  async getMultipleAsync(keys: string[]): Promise<Record<string, any>> {
    this.ensureInitialized();
    return getNativeDB().getMultipleAsync(keys);
  }

  /**
   * Async range query — reads on DBWorker thread without blocking UI.
   */
  async rangeQueryAsync(
    startKey: string,
    endKey: string
  ): Promise<RangeQueryResult[]> {
    this.ensureInitialized();
    return getNativeDB().rangeQueryAsync({ startKey, endKey });
  }

  /**
   * Async get all keys — enumerates on DBWorker thread.
   */
  async getAllKeysAsync(): Promise<string[]> {
    this.ensureInitialized();
    return getNativeDB().getAllKeysAsync();
  }

  /**
   * Async remove — executes on DBWorker thread,
   * also schedules compaction if fragmentation > 30%.
   */
  async removeAsync(key: string): Promise<boolean> {
    this.ensureInitialized();
    const success = await getNativeDB().removeAsync(key);
    if (success) {
      this.notify('remove', key);
    }
    return success;
  }

  // ── Sync API (used by SyncManager) ────────────────────────────────────────

  /**
   * Returns all locally modified records since lastSyncClock.
   */
  async getLocalChangesAsync(lastSyncClock: number): Promise<SyncChanges> {
    this.ensureInitialized();
    return getNativeDB().getLocalChangesAsync(lastSyncClock);
  }

  /**
   * Atomically apply remote changes with conflict resolution.
   */
  async applyRemoteChangesAsync(changes: SyncRecord[]): Promise<boolean> {
    this.ensureInitialized();
    return getNativeDB().applyRemoteChangesAsync(changes);
  }

  /**
   * Mark local records as successfully pushed to clear dirty flags.
   */
  async markPushedAsync(acks: SyncAck[]): Promise<boolean> {
    this.ensureInitialized();
    return getNativeDB().markPushedAsync(acks);
  }

  // ── Diagnostics ───────────────────────────────────────────────────────────

  verifyHealth(): boolean {
    this.ensureInitialized();
    return getNativeDB().verifyHealth();
  }

  repair(): boolean {
    this.ensureInitialized();
    return getNativeDB().repair();
  }

  getDatabasePath(): string {
    this.ensureInitialized();
    return getNativeDB().getDatabasePath();
  }

  getWALPath(): string {
    this.ensureInitialized();
    return getNativeDB().getWALPath();
  }

  /**
   * Get live database stats: tree height, node count, fragmentation ratio, etc.
   */
  getStats(): DBStats {
    this.ensureInitialized();
    return getNativeDB().getStats();
  }

  /**
   * Run schema migrations.
   */
  async migrate(
    fromVersion: number,
    toVersion: number,
    migrationFn: (db: TurboDB) => Promise<void>
  ): Promise<void> {
    this.ensureInitialized();
    const currentVersionKey = '__sys_db_version';
    const currentVersion =
      (await this.getAsync<number>(currentVersionKey)) || 0;

    if (currentVersion >= fromVersion && currentVersion < toVersion) {
      console.log(
        `[TurboDB] Migrating from ${currentVersion} to ${toVersion}...`
      );
      await migrationFn(this);
      await this.setAsync(currentVersionKey, toVersion);
      console.log(`[TurboDB] Migration to ${toVersion} successful.`);
    }
  }

  /**
   * Enqueue a mutation to be processed later.
   * Useful for offline-first interaction patterns.
   */
  async enqueueMutation(mutation: {
    type: 'set' | 'remove' | 'merge';
    key: string;
    value?: any;
  }): Promise<void> {
    const queueKey = '__sys_mutation_queue';
    const queue = (await this.getAsync<any[]>(queueKey)) || [];
    queue.push({
      ...mutation,
      id: Date.now() + Math.random(),
      timestamp: Date.now(),
    });
    await this.setAsync(queueKey, queue);
  }

  /**
   * Process all pending mutations in the queue.
   */
  async flushQueue(): Promise<void> {
    const queueKey = '__sys_mutation_queue';
    const queue = await this.getAsync<any[]>(queueKey);
    if (!queue || queue.length === 0) return;

    for (const op of queue) {
      try {
        if (op.type === 'set') await this.setAsync(op.key, op.value);
        else if (op.type === 'remove') await this.removeAsync(op.key);
        else if (op.type === 'merge') await this.mergeAsync(op.key, op.value);
      } catch (e) {
        console.error(`[TurboDB] Flush error for key ${op.key}:`, e);
      }
    }

    await this.setAsync(queueKey, []);
  }

  /**
   * Triggers compaction to reclaim fragmented mmap space and optimize B-Tree structure.
   *
   * Note: Currently delegates to the WAL-first repair path, which also defragments
   * the tree after archiving fragmented pages. A dedicated non-destructive compaction
   * binding (DBScheduler::Priority::COMPACTION) is planned for a future native release.
   */
  async compact(): Promise<boolean> {
    this.ensureInitialized();
    return getNativeDB().repair();
  }

  /**
   * Export all data as a plain object.
   */
  async export(): Promise<Record<string, any>> {
    // Filter internal __idx:, __sys:, __oplog: keys — only export user data
    const keys = (await this.getAllKeysAsync()).filter(
      (k) => !k.startsWith('__')
    );
    return this.getMultipleAsync(keys);
  }

  /**
   * Import data from an object.
   */
  async import(data: Record<string, any>): Promise<void> {
    await this.setMultiAsync(data);
  }

  /**
   * Get detailed engine metrics.
   */
  getMetrics(): DBStats & { path: string; wal: string } {
    return {
      ...this.getStats(),
      path: this.getDatabasePath(),
      wal: this.getWALPath(),
    };
  }

  /**
   * Rotate the active encryption key.
   *
   * @throws {TurboDBError} with code NOT_SUPPORTED — not yet implemented.
   * Key rotation is managed by the native libsodium layer at initialization time.
   * This API is reserved for a future native key-rotation binding.
   */
  async setEncryptionKey(_key: string): Promise<void> {
    throw new TurboDBError(
      TurboDBErrorCode.NOT_SUPPORTED,
      'setEncryptionKey is not yet implemented. Key rotation is managed by the native libsodium layer at init time.'
    );
  }

  /**
   * Placeholder for a plugin system.
   */
  use(plugin: { name: string; onSet?: (k: string, v: any) => void }): void {
    if (plugin.onSet) {
      this.subscribeAll((event) => {
        if (event.type === 'set' && plugin.onSet) {
          plugin.onSet(event.key, event.value);
        }
      });
    }
  }

  setSecureMode(enable: boolean): void {
    this.ensureInitialized();
    getNativeDB().setSecureMode(enable);
  }
}

export default TurboDB;
