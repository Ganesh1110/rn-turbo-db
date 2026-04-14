import NativeSecureDB from './NativeSecureDB';

declare const global: {
  NativeDB: {
    initStorage(path: string, size: number): boolean;
    insertRec(key: string, obj: any): boolean;
    findRec(key: string): any;
    clearStorage(): boolean;
    benchmark(): number;
  };
};

export class SecureDB {
  static install() {
    NativeSecureDB.install();
  }

  private isInitialized = false;

  constructor(private path: string, private size: number = 10 * 1024 * 1024) {}

  private ensureInitialized() {
    if (!this.isInitialized) {
      SecureDB.install();
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
