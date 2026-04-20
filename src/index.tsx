import { TurboModuleRegistry, NativeModules } from 'react-native';
import type { Spec } from './NativeTurboDB';

const NativeTurboDB =
  TurboModuleRegistry.get<Spec>('TurboDB') || (NativeModules as any).TurboDB;

declare const global: {
  NativeDB: {
    // ── Sync API ──
    initStorage(path: string, size: number): boolean;
    insertRec(key: string, obj: any): boolean;
    findRec(key: string): any;
    clearStorage(): boolean;
    setMulti(entries: Record<string, any>): boolean;
    getMultiple(keys: string[]): Record<string, any>;
    remove(key: string): boolean;
    del(key: string): boolean;
    deleteAll(): boolean;
    benchmark(): number;
    rangeQuery(startKey: string, endKey: string): Array<{ key: string; value: any }>;
    getAllKeys(): string[];
    getAllKeysPaged(limit: number, offset: number): string[];
    flush(): void;
    // ── Async API ──
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
    // ── Sync APIs ──
    getLocalChangesAsync(lastSyncClock: number): Promise<any>;
    applyRemoteChangesAsync(changes: any[]): Promise<boolean>;
    markPushedAsync(acks: any[]): Promise<boolean>;
    // ── Diagnostics ──
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
};

export type DBMode = 'secure' | 'turbo';

export { SyncManager } from './SyncManager';
export type { SyncAdapter, SyncRecord, SyncChanges, SyncAck, SyncOptions, SyncEvent } from './SyncManager';

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
    size: number = 10 * 1024 * 1024
  ): Promise<TurboDB> {
    const db = new TurboDB(path, size);
    await db._initAsync();
    return db;
  }

  /**
   * Install the native JSI global. Call once at app startup
   * (e.g., in index.js before any DB usage).
   */
  static install(): void {
    if (!NativeTurboDB) {
      console.error(
        "[TurboDB] Native module 'TurboDB' not found. " +
          'Rebuild the native app (npx react-native run-ios / run-android).'
      );
      return;
    }
    NativeTurboDB.install();
  }

  static getDocumentsDirectory(): string {
    if (!NativeTurboDB) return '';
    return NativeTurboDB.getDocumentsDirectory();
  }

  private isInitialized = false;

  /**
   * ⚠️ Prefer `TurboDB.create()` over the constructor.
   * If you use the constructor, call `ensureInitialized()` (sync) only
   * for simple use-cases that can tolerate blocking on first access.
   */
  constructor(
    private path: string,
    private size: number = 10 * 1024 * 1024
  ) {}

  /**
   * Async init — no busy-wait, returns Promise.
   * Called internally by `TurboDB.create()`.
   */
  private async _initAsync(): Promise<void> {
    if (this.isInitialized) return;

    if (!NativeTurboDB) {
      throw new Error('[TurboDB] Native module not found. Check your native build.');
    }

    TurboDB.install();

    if (typeof global.NativeDB === 'undefined') {
      throw new Error(
        '[TurboDB] NativeDB JSI object not found after install(). ' +
          'Check native logs for errors.'
      );
    }

    const success = global.NativeDB.initStorage(this.path, this.size);
    if (!success) {
      throw new Error(`[TurboDB] Failed to initialize storage at ${this.path}`);
    }
    this.isInitialized = true;
  }

  /**
   * Sync init — only for backward compat. Prefer async.
   * Does NOT use a busy-wait — throws immediately if NativeDB is not ready.
   */
  private ensureInitialized(): void {
    if (this.isInitialized) return;

    if (!NativeTurboDB) {
      throw new Error('[TurboDB] Native module not found. Check your native build.');
    }

    TurboDB.install();

    if (typeof global.NativeDB === 'undefined') {
      throw new Error(
        '[TurboDB] NativeDB not found. ' +
          'Use TurboDB.create() for async initialization.'
      );
    }

    const success = global.NativeDB.initStorage(this.path, this.size);
    if (!success) {
      throw new Error(`[TurboDB] Failed to initialize storage at ${this.path}`);
    }
    this.isInitialized = true;
  }

  // ── Synchronous API (fast path) ──────────────────────────────────────────

  has(key: string): boolean {
    this.ensureInitialized();
    return global.NativeDB.findRec(key) !== undefined;
  }

  set(key: string, value: any): boolean {
    this.ensureInitialized();
    return global.NativeDB.insertRec(key, value);
  }

  get<T = any>(key: string): T | undefined {
    this.ensureInitialized();
    return global.NativeDB.findRec(key);
  }

  setMulti(entries: Record<string, any>): boolean {
    this.ensureInitialized();
    return global.NativeDB.setMulti(entries);
  }

  getMultiple(keys: string[]): Record<string, any> {
    this.ensureInitialized();
    return global.NativeDB.getMultiple(keys);
  }

  remove(key: string): boolean {
    this.ensureInitialized();
    return global.NativeDB.remove(key);
  }

  /** Alias for remove() */
  del(key: string): boolean {
    return this.remove(key);
  }

  deleteAll(): boolean {
    this.ensureInitialized();
    return global.NativeDB.deleteAll();
  }

  benchmark(): number {
    this.ensureInitialized();
    return global.NativeDB.benchmark();
  }

  rangeQuery(startKey: string, endKey: string): RangeQueryResult[] {
    this.ensureInitialized();
    return global.NativeDB.rangeQuery(startKey, endKey);
  }

  getAllKeys(): string[] {
    this.ensureInitialized();
    return global.NativeDB.getAllKeys();
  }

  /** O(limit) paged enumeration — does NOT load all keys */
  getAllKeysPaged(limit: number, offset: number): string[] {
    this.ensureInitialized();
    return global.NativeDB.getAllKeysPaged(limit, offset);
  }

  clear(): boolean {
    this.ensureInitialized();
    return global.NativeDB.clearStorage();
  }

  flush(): void {
    this.ensureInitialized();
    global.NativeDB.flush();
  }

  // ── Asynchronous API (non-blocking — all run on DBWorker thread) ──────────

  /**
   * Async set — serializes value on JS thread, writes on DBWorker thread.
   * Does NOT block the JS thread.
   */
  async setAsync(key: string, value: any): Promise<boolean> {
    this.ensureInitialized();
    return global.NativeDB.setAsync({ key, value });
  }

  /**
   * Async get — reads on DBWorker thread, deserializes back on JS thread.
   */
  async getAsync<T = any>(key: string): Promise<T | undefined> {
    this.ensureInitialized();
    return global.NativeDB.getAsync(key);
  }

  /**
   * Async batch set — serializes all values on JS thread, writes in batch on DBWorker.
   * Ideal for 100+ record writes.
   */
  async setMultiAsync(entries: Record<string, any>): Promise<boolean> {
    this.ensureInitialized();
    return global.NativeDB.setMultiAsync(entries);
  }

  /**
   * Async batch get — reads on DBWorker thread, returns results map.
   */
  async getMultipleAsync(keys: string[]): Promise<Record<string, any>> {
    this.ensureInitialized();
    return global.NativeDB.getMultipleAsync(keys);
  }

  /**
   * Async range query — reads on DBWorker thread without blocking UI.
   */
  async rangeQueryAsync(
    startKey: string,
    endKey: string
  ): Promise<RangeQueryResult[]> {
    this.ensureInitialized();
    return global.NativeDB.rangeQueryAsync({ startKey, endKey });
  }

  /**
   * Async get all keys — enumerates on DBWorker thread.
   */
  async getAllKeysAsync(): Promise<string[]> {
    this.ensureInitialized();
    return global.NativeDB.getAllKeysAsync();
  }

  /**
   * Async remove — executes on DBWorker thread,
   * also schedules compaction if fragmentation > 30%.
   */
  async removeAsync(key: string): Promise<boolean> {
    this.ensureInitialized();
    return global.NativeDB.removeAsync(key);
  }

  // ── Diagnostics ───────────────────────────────────────────────────────────

  verifyHealth(): boolean {
    this.ensureInitialized();
    return global.NativeDB.verifyHealth();
  }

  repair(): boolean {
    this.ensureInitialized();
    return global.NativeDB.repair();
  }

  getDatabasePath(): string {
    this.ensureInitialized();
    return global.NativeDB.getDatabasePath();
  }

  getWALPath(): string {
    this.ensureInitialized();
    return global.NativeDB.getWALPath();
  }

  /**
   * Get live database stats: tree height, node count, fragmentation ratio, etc.
   */
  getStats(): DBStats {
    this.ensureInitialized();
    return global.NativeDB.getStats();
  }

  setSecureMode(enable: boolean): void {
    this.ensureInitialized();
    global.NativeDB.setSecureMode(enable);
  }
}

export default TurboDB;
