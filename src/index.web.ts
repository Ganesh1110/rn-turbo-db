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
 * Features:
 * - SSR Friendly (No-op on server)
 * - IndexedDB Backend for persistence
 * - In-memory cache for synchronous reads
 * - Configurable immediate vs debounced writes
 * - Full API parity with React Native implementation
 */
export class TurboDB {
  static install(): boolean {
    return true; // No-op on web
  }

  static getDocumentsDirectory(): string {
    return '/';
  }

  /**
   * ✅ Preferred factory — async, ensures IndexedDB is ready.
   */
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

  constructor(
    private path: string,
    _size: number = 10 * 1024 * 1024,
    _options: DBOptions = {}
  ) {
    if (IS_SERVER) {
      console.log('TurboDB (Web): SSR Mode');
    } else {
      // Lazy-trigger initialization if not created via factory
      this.initPromise = this.ensureInitialized();
    }
  }

  private async ensureInitialized(): Promise<void> {
    if (IS_SERVER) return;
    if (this.isInitialized) return;
    if (this.initPromise) return this.initPromise;

    this.initPromise = new Promise((resolve, reject) => {
      const dbName = `turbodb_${this.path.replace(/\//g, '_')}`;
      const request = indexedDB.open(dbName, 1);

      request.onerror = () => {
        const error = new TurboDBError(
          TurboDBErrorCode.IO_FAIL,
          'Failed to open IndexedDB'
        );
        reject(error);
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

    return this.initPromise;
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
    this.saveTimeout = setTimeout(() => this.persistToIndexedDB(), 100);
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

  // --- Synchronous API ---

  has(key: string): boolean {
    return this.storage.has(key);
  }

  get<T = any>(key: string): T | undefined {
    return this.storage.get(key);
  }

  /**
   * Synchronously set a value (updates cache immediately, persists in background)
   */
  set(key: string, value: any, options: SetOptions = {}): boolean {
    if (IS_SERVER) return false;
    this.storage.set(key, value);

    if (options.debounce) {
      this.scheduleSave();
    } else {
      this.persistToIndexedDB().catch((e) => console.error(e));
    }
    return true;
  }

  remove(key: string): boolean {
    if (IS_SERVER) return false;
    const res = this.storage.delete(key);
    this.scheduleSave();
    return res;
  }

  clear(): boolean {
    if (IS_SERVER) return false;
    this.storage.clear();
    this.scheduleSave();
    return true;
  }

  getAllKeys(): string[] {
    return Array.from(this.storage.keys());
  }

  getAllKeysPaged(limit: number, offset: number): string[] {
    const keys = Array.from(this.storage.keys());
    return keys.slice(offset, offset + limit);
  }

  // --- Asynchronous API (Parity with Native) ---

  async setAsync(
    key: string,
    value: any,
    options: SetOptions = {}
  ): Promise<boolean> {
    await this.ensureInitialized();
    this.storage.set(key, value);
    if (options.debounce) {
      this.scheduleSave();
      return true;
    }
    await this.persistToIndexedDB();
    return true;
  }

  async getAsync<T = any>(key: string): Promise<T | undefined> {
    await this.ensureInitialized();
    return this.storage.get(key);
  }

  async setMultiAsync(
    entries: Record<string, any>,
    options: SetOptions = {}
  ): Promise<boolean> {
    if (IS_SERVER) return false;
    await this.ensureInitialized();
    for (const [key, value] of Object.entries(entries)) {
      this.storage.set(key, value);
    }
    if (options.debounce) {
      this.scheduleSave();
      return true;
    }
    await this.persistToIndexedDB();
    return true;
  }

  async getMultipleAsync(keys: string[]): Promise<Record<string, any>> {
    await this.ensureInitialized();
    const result: Record<string, any> = {};
    for (const key of keys) {
      result[key] = this.storage.get(key);
    }
    return result;
  }

  async rangeQueryAsync(
    startKey: string,
    endKey: string
  ): Promise<RangeQueryResult[]> {
    await this.ensureInitialized();
    const results: RangeQueryResult[] = [];
    const keys = Array.from(this.storage.keys()).sort();
    for (const key of keys) {
      if (key >= startKey && key <= endKey) {
        results.push({ key, value: this.storage.get(key) });
      }
    }
    return results;
  }

  async getAllKeysAsync(): Promise<string[]> {
    await this.ensureInitialized();
    return Array.from(this.storage.keys());
  }

  async removeAsync(key: string): Promise<boolean> {
    await this.ensureInitialized();
    const res = this.storage.delete(key);
    await this.persistToIndexedDB();
    return res;
  }

  async deleteAllAsync(): Promise<boolean> {
    await this.ensureInitialized();
    this.storage.clear();
    await this.persistToIndexedDB();
    return true;
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

  // --- Legacy Compatibility ---
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

  async rangeQuery(
    startKey: string,
    endKey: string
  ): Promise<RangeQueryResult[]> {
    return this.rangeQueryAsync(startKey, endKey);
  }

  benchmark(): number {
    return 0;
  }
}

export default TurboDB;
