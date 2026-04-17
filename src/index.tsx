import { TurboModuleRegistry, NativeModules } from 'react-native';
import type { Spec } from './NativeTurboDB';

const NativeTurboDB =
  TurboModuleRegistry.get<Spec>('TurboDB') || (NativeModules as any).TurboDB;

console.log('TurboDB: NativeTurboDB module:', NativeTurboDB);

declare const global: {
  NativeDB: {
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
    rangeQuery(
      startKey: string,
      endKey: string
    ): Array<{ key: string; value: any }>;
    getAllKeys(): string[];
    getAllKeysPaged(limit: number, offset: number): string[];
    flush(): void;
    // Async variants
    setMultiAsync(entries: Record<string, any>): Promise<boolean>;
    getMultipleAsync(keys: string[]): Promise<Record<string, any>>;
    rangeQueryAsync(args: {
      startKey: string;
      endKey: string;
    }): Promise<Array<{ key: string; value: any }>>;
    getAllKeysAsync(): Promise<string[]>;
  };
};

export type DBMode = 'secure' | 'turbo';

export interface RangeQueryResult {
  key: string;
  value: any;
}

export class TurboDB {
  static install(): void {
    console.log('TurboDB.install: called');
    if (!NativeTurboDB) {
      console.error(
        "TurboDB: Native module 'TurboDB' not found. " +
          'Ensure you have rebuilt the native app (npx react-native run-ios / run-android) ' +
          'and that the module is correctly linked.'
      );
      return;
    }
    console.log('TurboDB.install: calling NativeTurboDB.install()');
    NativeTurboDB.install();
    console.log(
      'TurboDB.install: done, global.NativeDB =',
      typeof global.NativeDB
    );
  }

  static getDocumentsDirectory(): string {
    if (!NativeTurboDB) return '';
    return NativeTurboDB.getDocumentsDirectory();
  }

  private isInitialized = false;

  constructor(
    private path: string,
    private size: number = 10 * 1024 * 1024
  ) {}

  private ensureInitialized() {
    console.log(
      'TurboDB.ensureInitialized: start, isInitialized =',
      this.isInitialized
    );
    if (!this.isInitialized) {
      if (!NativeTurboDB) {
        throw new Error(
          'TurboDB: Native module not found. Check your native build.'
        );
      }

      // Try to install and check for NativeDB
      for (let i = 0; i < 3; i++) {
        TurboDB.install();
        console.log(
          'TurboDB.ensureInitialized: install attempt',
          i,
          'global.NativeDB =',
          typeof global.NativeDB
        );
        if (typeof global.NativeDB !== 'undefined') {
          break;
        }
        // Small delay if not found immediately (for New Arch async behavior)
        // We use a busy-wait since this is synchronous initialization
        const start = Date.now();
        while (Date.now() - start < 10) {}
      }

      if (typeof global.NativeDB === 'undefined') {
        throw new Error(
          'TurboDB: NativeDB JSI object not found after install(). Check native logs.'
        );
      }

      console.log(
        'TurboDB.ensureInitialized: calling initStorage',
        this.path,
        this.size
      );
      const success = global.NativeDB.initStorage(this.path, this.size);
      console.log('TurboDB.ensureInitialized: initStorage result =', success);
      if (!success) {
        throw new Error(`Failed to initialize TurboDB at ${this.path}`);
      }
      this.isInitialized = true;
      console.log(
        'TurboDB.ensureInitialized: done, isInitialized =',
        this.isInitialized
      );
    }
  }

  // --- Synchronous API ---

  has(key: string): boolean {
    this.ensureInitialized();
    console.log('TurboDB.has:', key);
    return global.NativeDB.findRec(key) !== undefined;
  }

  set(key: string, value: any): boolean {
    this.ensureInitialized();
    console.log('TurboDB.set:', key, 'value:', typeof value);
    return global.NativeDB.insertRec(key, value);
  }

  get<T = any>(key: string): T | undefined {
    this.ensureInitialized();
    console.log('TurboDB.get:', key);
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

  // --- Asynchronous API ---

  async setAsync(key: string, value: any): Promise<boolean> {
    return this.set(key, value);
  }

  async getAsync<T = any>(key: string): Promise<T | undefined> {
    return this.get<T>(key);
  }

  async setMultiAsync(entries: Record<string, any>): Promise<boolean> {
    this.ensureInitialized();
    return global.NativeDB.setMultiAsync(entries);
  }

  async getMultipleAsync(keys: string[]): Promise<Record<string, any>> {
    this.ensureInitialized();
    return global.NativeDB.getMultipleAsync(keys);
  }

  async rangeQueryAsync(
    startKey: string,
    endKey: string
  ): Promise<RangeQueryResult[]> {
    this.ensureInitialized();
    return global.NativeDB.rangeQueryAsync({ startKey, endKey });
  }

  async getAllKeysAsync(): Promise<string[]> {
    this.ensureInitialized();
    return global.NativeDB.getAllKeysAsync();
  }
}

export default TurboDB;
