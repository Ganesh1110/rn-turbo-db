import { TurboModuleRegistry } from 'react-native';
import type { Spec } from './NativeSecureDB';

const NativeSecureDB = TurboModuleRegistry.get<Spec>('SecureDB');

declare const global: {
  NativeDB: {
    initStorage(path: string, size: number): boolean;
    insertRec(key: string, obj: any): boolean;
    findRec(key: string): any;
    clearStorage(): boolean;
    benchmark(): number;
    setMulti(entries: Record<string, any>): boolean;
    getMultiple(keys: string[]): Record<string, any>;
    remove(key: string): boolean;
    rangeQuery(
      startKey: string,
      endKey: string
    ): Array<{ key: string; value: any }>;
    getAllKeys(): string[];
  };
};

export interface SecureDBConfig {
  path: string;
  size?: number;
  maxKeys?: number;
  keySize?: number;
}

export interface RangeQueryResult {
  key: string;
  value: any;
}

export class SecureDB {
  static install() {
    if (!NativeSecureDB) {
      console.error(
        "SecureDB: Native module 'SecureDB' not found. " +
          'Ensure you have rebuilt the native app (npx react-native run-ios / run-android) ' +
          'and that the module is correctly linked.'
      );
      return;
    }
    NativeSecureDB.install();
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
      SecureDB.install();

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

  benchmark(): number {
    this.ensureInitialized();
    return global.NativeDB.benchmark();
  }
}

export default SecureDB;
