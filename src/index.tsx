import { TurboModuleRegistry } from 'react-native';
import type { Spec } from './NativeSecureDB';

const NativeSecureDB = TurboModuleRegistry.get<Spec>('SecureDB');

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
    flush(): void;
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

export interface SecureDBConfig {
  path: string;
  size?: number;
  maxKeys?: number;
  keySize?: number;
  mode?: DBMode;
}

export interface RangeQueryResult {
  key: string;
  value: any;
}

export class SecureDB {
  static install(): boolean {
    if (!NativeSecureDB) {
      console.error(
        "SecureDB: Native module 'SecureDB' not found. " +
          'Ensure you have rebuilt the native app (npx react-native run-ios / run-android) ' +
          'and that the module is correctly linked.'
      );
      return false;
    }
    return NativeSecureDB.install();
  }

  static getDocumentsDirectory(): string {
    if (!NativeSecureDB) return '';
    return NativeSecureDB.getDocumentsDirectory();
  }

  private isInitialized = false;

  constructor(
    private path: string,
    private size: number = 10 * 1024 * 1024
  ) {}

  private ensureInitialized() {
    if (!this.isInitialized) {
      if (!NativeSecureDB) {
        throw new Error(
          'SecureDB: Native module not found. Check your native build.'
        );
      }

      // Try to install and check for NativeDB
      for (let i = 0; i < 3; i++) {
        SecureDB.install();
        if (typeof global.NativeDB !== 'undefined') {
          break;
        }
        // Small delay if not found immediately (for New Arch async behavior)
        // Note: blocking in JSI is not ideal but this is initialization
      }

      if (typeof global.NativeDB === 'undefined') {
        throw new Error(
          'SecureDB: NativeDB JSI object not found after install(). Check native logs.'
        );
      }

      const success = global.NativeDB.initStorage(this.path, this.size);
      if (!success) {
        throw new Error(`Failed to initialize SecureDB at ${this.path}`);
      }
      this.isInitialized = true;
    }
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

  del(key: string): boolean {
    this.ensureInitialized();
    return global.NativeDB.del(key);
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

  clear(): boolean {
    this.ensureInitialized();
    return global.NativeDB.clearStorage();
  }

  flush(): void {
    this.ensureInitialized();
    global.NativeDB.flush();
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

export default SecureDB;
