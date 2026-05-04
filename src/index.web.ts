import { TurboDBError, TurboDBErrorCode } from './TurboDBError';

export interface RangeQueryResult {
  key: string;
  value: any;
}

const IS_SERVER = typeof window === 'undefined';

export interface DBOptions {
  syncEnabled?: boolean;
}

export interface SetOptions {
  debounce?: boolean;
}

/**
 * Web Implementation of TurboDB.
 */
export class TurboDB {
  static install(): boolean {
    return true; // No-op on web
  }

  static getDocumentsDirectory(): string {
    return '/';
  }

  static async create(
    path: string,
    size: number = 10 * 1024 * 1024,
    options: DBOptions = {}
  ): Promise<TurboDB> {
    const instance = new TurboDB(path, size, options);
    await instance.ensureInitialized();
    return instance;
  }

  private isInitialized = false;
  private storage: Map<string, any> = new Map();
  private db: IDBDatabase | null = null;
  private saveTimeout: any = null;
  private initPromise: Promise<void> | null = null;

  private listeners: Set<
    (event: { type: 'set' | 'remove'; key: string; value?: any }) => void
  > = new Set();
  private keyListeners: Map<string, Set<(value: any) => void>> = new Map();
  private indexes: Set<string> = new Set();

  constructor(
    private path: string,
    _size: number = 10 * 1024 * 1024,
    _options: DBOptions = {}
  ) {
    if (!IS_SERVER) {
      // Kick off init in background — do NOT await here.
      // All public methods call ensureInitialized() which returns the same promise.
      // This avoids a re-entrant call: constructor sets initPromise FIRST.
      this.initPromise = this._openIndexedDB();
    }
  }

  /**
   * Ensures IndexedDB is open and data is loaded.
   * Thread-safe: concurrent callers all receive the same in-flight Promise.
   */
  private async ensureInitialized(): Promise<void> {
    if (IS_SERVER || this.isInitialized) return;
    // If init is already in flight (e.g., from constructor), return that same promise.
    // Do NOT start a new one — this prevents the recursive loop.
    if (this.initPromise) return this.initPromise;

    // Should never reach here (constructor sets initPromise), but guard defensively.
    this.initPromise = this._openIndexedDB();
    return this.initPromise;
  }

  /**
   * Internal: Opens IndexedDB, creates schema if needed, loads data.
   * Must only be called ONCE; result stored in initPromise.
   */
  private _openIndexedDB(): Promise<void> {
    return new Promise((resolve, reject) => {
      const dbName = `turbodb_${this.path.replace(/\//g, '_')}`;
      const request = indexedDB.open(dbName, 1);

      request.onerror = () => {
        reject(
          new TurboDBError(TurboDBErrorCode.IO_FAIL, 'Failed to open IndexedDB')
        );
      };

      request.onupgradeneeded = (event: any) => {
        const db = event.target.result;
        if (!db.objectStoreNames.contains('kv')) {
          db.createObjectStore('kv');
        }
      };

      request.onsuccess = async (event: any) => {
        this.db = event.target.result;
        try {
          await this.loadFromIndexedDB();
          this.isInitialized = true;
          resolve();
        } catch (e: any) {
          reject(e);
        }
      };
    });
  }

  private async loadFromIndexedDB(): Promise<void> {
    if (!this.db) return;
    return new Promise((resolve, reject) => {
      try {
        const transaction = this.db!.transaction(['kv'], 'readonly');
        const store = transaction.objectStore('kv');
        const request = store.openCursor();
        request.onsuccess = (event: any) => {
          const cursor = event.target.result;
          if (cursor) {
            this.storage.set(cursor.key, cursor.value);
            cursor.continue();
          } else {
            resolve();
          }
        };
        request.onerror = () => {
          reject(
            new TurboDBError(
              TurboDBErrorCode.IO_FAIL,
              'Failed to read from IndexedDB'
            )
          );
        };
      } catch (e: any) {
        reject(new TurboDBError(TurboDBErrorCode.IO_FAIL, e.message));
      }
    });
  }

  private scheduleSave() {
    if (IS_SERVER) return;
    if (this.saveTimeout) clearTimeout(this.saveTimeout);
    this.saveTimeout = setTimeout(
      () =>
        this.persistToIndexedDB().catch((e) =>
          console.error('[TurboDB Web] scheduleSave persist failed:', e)
        ),
      100
    );
  }

  private async persistToIndexedDB(): Promise<void> {
    if (!this.db || IS_SERVER) return;
    return new Promise((resolve, reject) => {
      try {
        const transaction = this.db!.transaction(['kv'], 'readwrite');
        const store = transaction.objectStore('kv');
        store.clear();
        for (const [key, value] of this.storage.entries()) {
          store.put(value, key);
        }
        transaction.oncomplete = () => resolve();
        transaction.onerror = (event: any) => {
          const error = event.target.error;
          if (error && error.name === 'QuotaExceededError') {
            reject(
              new TurboDBError(
                TurboDBErrorCode.QUOTA_EXCEEDED,
                'Storage quota exceeded'
              )
            );
          } else {
            reject(
              new TurboDBError(TurboDBErrorCode.IO_FAIL, 'Persistence failed')
            );
          }
        };
      } catch (e: any) {
        reject(new TurboDBError(TurboDBErrorCode.IO_FAIL, e.message));
      }
    });
  }

  // --- Subscriptions & Notifications ---

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
    if (kListeners) kListeners.forEach((cb) => cb(value));
  }

  // ── R4: Live Query API ──────────────────────────────────────

  watchKey(key: string, callback: (value: any) => void): () => void {
    callback(this.get(key));
    return this.subscribe(key, callback);
  }

  watchPrefix(
    prefix: string,
    callback: (results: RangeQueryResult[]) => void,
    options: { debounceMs?: number } = {}
  ): () => void {
    const debounceMs = options.debounceMs ?? 0;
    let timeout: ReturnType<typeof setTimeout> | null = null;
    let isActive = true;

    const rerun = () => {
      if (!isActive) return;
      Promise.resolve(this.getByPrefix(prefix))
        .then(callback)
        .catch(console.error);
    };

    const schedule = () => {
      if (debounceMs > 0) {
        if (timeout) clearTimeout(timeout);
        timeout = setTimeout(rerun, debounceMs);
      } else {
        rerun();
      }
    };

    rerun();

    const unsub = this.subscribeAll((event) => {
      if (event.key.startsWith(prefix) || prefix === '') schedule();
    });

    return () => {
      isActive = false;
      if (timeout) clearTimeout(timeout);
      unsub();
    };
  }

  watchQuery<T>(
    queryFn: () => Promise<T>,
    callback: (result: T) => void,
    options: { debounceMs?: number } = {}
  ): () => void {
    const debounceMs = options.debounceMs ?? 100;
    let timeout: ReturnType<typeof setTimeout> | null = null;
    let isActive = true;

    const rerun = () => {
      if (!isActive) return;
      queryFn().then(callback).catch(console.error);
    };

    const schedule = () => {
      if (debounceMs > 0) {
        if (timeout) clearTimeout(timeout);
        timeout = setTimeout(rerun, debounceMs);
      } else {
        rerun();
      }
    };

    rerun();
    const unsub = this.subscribeAll(() => schedule());
    return () => {
      isActive = false;
      if (timeout) clearTimeout(timeout);
      unsub();
    };
  }

  // --- Basic API ---

  async init(): Promise<void> {
    return this.ensureInitialized();
  }

  has(key: string): boolean {
    // Use get() rather than storage.has() so TTL-expired keys correctly return false
    return this.get(key) !== undefined;
  }

  get<T = any>(key: string): T | undefined {
    const val = this.storage.get(key);
    if (val && typeof val === 'object' && '__ttl_expiry' in val) {
      if (Date.now() > val.__ttl_expiry) {
        this.remove(key);
        return undefined;
      }
      return val.data;
    }
    return val;
  }

  set(key: string, value: any, options: SetOptions = {}): boolean {
    if (IS_SERVER) return false;
    this.storage.set(key, value);
    this.indexes.forEach((field) => {
      if (value && value[field] !== undefined) {
        this.storage.set(`__idx:${field}:${value[field]}:${key}`, key);
      }
    });
    this.notify('set', key, value);
    if (options.debounce) {
      this.scheduleSave();
    } else {
      // BUG-8 fix: surface persistence errors via the error notify channel
      this.persistToIndexedDB().catch((e) => {
        console.error('[TurboDB Web] persist failed:', e);
        // Fire a synthetic error event so subscribers can react
        this.listeners.forEach((cb) =>
          cb({ type: 'remove' as any, key: '__error__', value: e })
        );
      });
    }
    return true;
  }

  setWithTTL(
    key: string,
    value: any,
    ttlMs: number,
    options: SetOptions = {}
  ): boolean {
    return this.set(
      key,
      { data: value, __ttl_expiry: Date.now() + ttlMs },
      options
    );
  }

  /**
   * Async version of setWithTTL — stores TTL sidecar alongside the value.
   */
  async setWithTTLAsync(
    key: string,
    value: any,
    ttlMs: number,
    options: SetOptions = {}
  ): Promise<boolean> {
    await this.ensureInitialized();
    return this.setWithTTL(key, value, ttlMs, options);
  }

  /**
   * Scan all keys and remove expired TTL entries.
   * Returns number of records cleaned.
   */
  async cleanupExpiredAsync(): Promise<number> {
    await this.ensureInitialized();
    const keys = Array.from(this.storage.keys());
    let cleaned = 0;
    const now = Date.now();
    for (const key of keys) {
      const val = this.storage.get(key);
      if (val && typeof val === 'object' && '__ttl_expiry' in val) {
        if (now > val.__ttl_expiry) {
          this.storage.delete(key);
          this.scheduleSave();
          cleaned++;
        }
      }
    }
    return cleaned;
  }

  remove(key: string): boolean {
    if (IS_SERVER) return false;
    const res = this.storage.delete(key);
    if (res) {
      this.notify('remove', key);
      this.scheduleSave();
    }
    return res;
  }

  clear(): boolean {
    if (IS_SERVER) return false;
    this.storage.clear();
    this.notify('remove', '*');
    this.scheduleSave();
    return true;
  }

  length(): number {
    // Exclude internal __ keys from user-visible length count
    let count = 0;
    for (const key of this.storage.keys()) {
      if (!key.startsWith('__')) count++;
    }
    return count;
  }

  getAllKeys(): string[] {
    // Exclude internal __ keys — mirrors native getAllKeys() behavior
    return Array.from(this.storage.keys()).filter((k) => !k.startsWith('__'));
  }

  // --- Medium & Advanced API ---

  async createIndex(field: string): Promise<void> {
    this.indexes.add(field);
    for (const [key, val] of this.storage.entries()) {
      if (val && val[field] !== undefined) {
        this.storage.set(`__idx:${field}:${val[field]}:${key}`, key);
      }
    }
  }

  async queryByIndex(field: string, value: any): Promise<any[]> {
    const prefix = `__idx:${field}:${value}:`;
    const indexResults = this.getByPrefix(prefix);
    const results = [];
    for (const idx of indexResults) {
      const data = this.storage.get(idx.value);
      if (data) results.push(data);
    }
    return results;
  }

  async *streamKeys(): AsyncGenerator<string> {
    await this.ensureInitialized();
    for (const key of this.storage.keys()) {
      if (key.startsWith('__')) continue; // skip ALL internal prefixes
      yield key;
    }
  }

  merge(
    key: string,
    partial: Record<string, any>,
    options: SetOptions = {}
  ): boolean {
    const current = this.get(key) || {};
    return this.set(key, { ...current, ...partial }, options);
  }

  removeMultiple(keys: string[]): boolean {
    let allSuccess = true;
    for (const key of keys) {
      if (!this.remove(key)) allSuccess = false;
    }
    return allSuccess;
  }

  getOrDefault<T = any>(key: string, defaultValue: T): T {
    const val = this.get<T>(key);
    return val !== undefined ? val : defaultValue;
  }

  setIfNotExists(key: string, value: any, options: SetOptions = {}): boolean {
    if (this.has(key)) return false;
    return this.set(key, value, options);
  }

  compareAndSet(
    key: string,
    expected: any,
    next: any,
    options: SetOptions = {}
  ): boolean {
    const current = this.get(key);
    if (JSON.stringify(current) === JSON.stringify(expected)) {
      return this.set(key, next, options);
    }
    return false;
  }

  rangeQuery(startKey: string, endKey: string): RangeQueryResult[] {
    const results: RangeQueryResult[] = [];
    const keys = Array.from(this.storage.keys()).sort();
    for (const key of keys) {
      if (key >= startKey && key <= endKey) {
        results.push({ key, value: this.storage.get(key) });
      }
    }
    return results;
  }

  getByPrefix(prefix: string): RangeQueryResult[] {
    return this.rangeQuery(prefix, prefix + '\uffff');
  }

  // --- Asynchronous API ---

  async setAsync(
    key: string,
    value: any,
    options: SetOptions = {}
  ): Promise<boolean> {
    await this.ensureInitialized();
    return this.set(key, value, options);
  }

  async getAsync<T = any>(key: string): Promise<T | undefined> {
    await this.ensureInitialized();
    return this.get<T>(key);
  }

  async removeAsync(key: string): Promise<boolean> {
    await this.ensureInitialized();
    return this.remove(key);
  }

  async setMultiAsync(
    entries: Record<string, any>,
    options: SetOptions = {}
  ): Promise<boolean> {
    await this.ensureInitialized();
    for (const [key, value] of Object.entries(entries)) {
      this.set(key, value, options);
    }
    return true;
  }

  async getMultipleAsync(keys: string[]): Promise<Record<string, any>> {
    await this.ensureInitialized();
    const result: Record<string, any> = {};
    for (const key of keys) {
      result[key] = this.get(key);
    }
    return result;
  }

  async rangeQueryAsync(
    startKey: string,
    endKey: string
  ): Promise<RangeQueryResult[]> {
    await this.ensureInitialized();
    return this.rangeQuery(startKey, endKey);
  }

  async getByPrefixAsync(prefix: string): Promise<RangeQueryResult[]> {
    return this.rangeQueryAsync(prefix, prefix + '\uffff');
  }

  /**
   * Search keys matching a regex pattern (JS-side regex, web parity).
   */
  async regexSearchAsync(pattern: string): Promise<RangeQueryResult[]> {
    await this.ensureInitialized();
    const re = new RegExp(pattern);
    const results: RangeQueryResult[] = [];
    for (const [key] of this.storage.entries()) {
      if (key.startsWith('__')) continue;
      if (re.test(key)) {
        const resolved = this.get(key);
        if (resolved !== undefined) results.push({ key, value: resolved });
      }
    }
    return results;
  }

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
      results = Array.from(this.storage.entries()).map(([key, value]) => ({
        key,
        value,
      }));
    }
    let items = results.map((r) => r.value);
    if (options.filter) items = items.filter(options.filter);
    if (options.sort) items = items.sort(options.sort);
    if (options.limit) items = items.slice(0, options.limit);
    return items;
  }

  /**
   * Wraps a set of writes.
   * ⚠️ NOT atomic on web — no rollback on error. Use setMultiAsync() for batch safety.
   * @deprecated Use setMultiAsync() for safe batch operations.
   */
  async transaction(fn: (db: TurboDB) => Promise<void> | void): Promise<void> {
    try {
      await fn(this);
      await this.flush();
    } catch (e) {
      console.error('[TurboDB Web] transaction(): no rollback on error.', e);
      throw e;
    }
  }

  async getAllKeysAsync(): Promise<string[]> {
    await this.ensureInitialized();
    return this.getAllKeys();
  }

  async lengthAsync(): Promise<number> {
    await this.ensureInitialized();
    return this.length();
  }

  async mergeAsync(
    key: string,
    partial: Record<string, any>,
    options: SetOptions = {}
  ): Promise<boolean> {
    await this.ensureInitialized();
    return this.merge(key, partial, options);
  }

  async removeMultipleAsync(keys: string[]): Promise<boolean> {
    await this.ensureInitialized();
    return this.removeMultiple(keys);
  }

  async deleteAllAsync(): Promise<boolean> {
    await this.ensureInitialized();
    return this.clear();
  }

  async migrate(
    fromVersion: number,
    toVersion: number,
    migrationFn: (db: TurboDB) => Promise<void>
  ): Promise<void> {
    const currentVersionKey = '__sys_db_version';
    const currentVersion =
      (await this.getAsync<number>(currentVersionKey)) || 0;
    if (currentVersion >= fromVersion && currentVersion < toVersion) {
      await migrationFn(this);
      await this.setAsync(currentVersionKey, toVersion);
    }
  }

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

  async flushQueue(): Promise<void> {
    const queueKey = '__sys_mutation_queue';
    const queue = await this.getAsync<any[]>(queueKey);
    if (!queue || queue.length === 0) return;
    for (const op of queue) {
      if (op.type === 'set') await this.setAsync(op.key, op.value);
      else if (op.type === 'remove') await this.removeAsync(op.key);
      else if (op.type === 'merge') await this.mergeAsync(op.key, op.value);
    }
    await this.setAsync(queueKey, []);
  }

  async compact(): Promise<boolean> {
    await this.persistToIndexedDB();
    return true;
  }

  async compactAsync(): Promise<boolean> {
    return this.compact();
  }

  async export(): Promise<Record<string, any>> {
    await this.ensureInitialized();
    const res: Record<string, any> = {};
    for (const [k, v] of this.storage.entries()) {
      if (!k.startsWith('__')) res[k] = v;
    }
    return res;
  }

  async import(data: Record<string, any>): Promise<number> {
    await this.setMultiAsync(data);
    return Object.keys(data).length;
  }

  /**
   * Store a binary blob. On web, stores the base64 string or Uint8Array as-is in IndexedDB.
   */
  async setBlobAsync(
    key: string,
    data: string | Uint8Array,
    options: SetOptions = {}
  ): Promise<boolean> {
    await this.ensureInitialized();
    let b64: string;
    if (typeof data === 'string') {
      b64 = data;
    } else {
      let binary = '';
      for (let i = 0; i < data.length; i++)
        binary += String.fromCharCode(data[i]!);
      b64 = btoa(binary);
    }
    return this.set(`__blob:${key}`, { __blob: true, data: b64 }, options);
  }

  /**
   * Retrieve a blob as a base64 string.
   */
  async getBlobAsync(key: string): Promise<string | null> {
    await this.ensureInitialized();
    const val = this.storage.get(`__blob:${key}`);
    if (!val || typeof val !== 'object' || !val.__blob) return null;
    return val.data as string;
  }

  getMetrics(): any {
    return {
      type: 'web',
      size: this.storage.size,
      initialized: this.isInitialized,
    };
  }

  use(plugin: { name: string; onSet?: (k: string, v: any) => void }): void {
    if (plugin.onSet) {
      this.subscribeAll((event) => {
        if (event.type === 'set' && plugin.onSet) {
          plugin.onSet(event.key, event.value);
        }
      });
    }
  }

  async flush(): Promise<void> {
    if (IS_SERVER) return;
    await this.ensureInitialized();
    if (this.saveTimeout) {
      clearTimeout(this.saveTimeout);
      this.saveTimeout = null;
    }
    await this.persistToIndexedDB();
  }

  // --- Sync APIs (Stubs for parity) ---
  async getLocalChangesAsync(_lastSyncClock: number): Promise<any> {
    return null;
  }
  async applyRemoteChangesAsync(_changes: any[]): Promise<boolean> {
    return true;
  }
  async markPushedAsync(_acks: any[]): Promise<boolean> {
    return true;
  }

  // --- Legacy Compatibility & Aliases ---
  async del(key: string): Promise<boolean> {
    return this.removeAsync(key);
  }

  async deleteAll(): Promise<boolean> {
    return this.deleteAllAsync();
  }

  async setMulti(entries: Record<string, any>): Promise<boolean> {
    return this.setMultiAsync(entries);
  }

  async getMultiple(keys: string[]): Promise<Record<string, any>> {
    return this.getMultipleAsync(keys);
  }

  benchmark(): number {
    return 0;
  }

  /**
   * @throws {TurboDBError} NOT_SUPPORTED — key rotation is not available on web.
   */
  async setEncryptionKey(_key: string): Promise<void> {
    throw new TurboDBError(
      TurboDBErrorCode.NOT_SUPPORTED,
      'setEncryptionKey is not supported on the web platform.'
    );
  }

  // --- Diagnostic stubs for API parity with native ---
  verifyHealth(): boolean {
    return this.isInitialized;
  }
  repair(): boolean {
    return true;
  }
  getDatabasePath(): string {
    return this.path;
  }
  getWALPath(): string {
    return '';
  }
  getStats() {
    return {
      treeHeight: 0,
      nodeCount: this.storage.size,
      formatVersion: 1,
      fragmentationRatio: 0,
      isInitialized: this.isInitialized,
      secureMode: false,
    };
  }
  setSecureMode(_enable: boolean): void {
    /* no-op on web */
  }
}

export default TurboDB;
