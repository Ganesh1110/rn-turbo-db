export interface RangeQueryResult {
  key: string;
  value: any;
}

export class SecureDB {
  static install(): boolean {
    return true; // No-op on web
  }

  static getDocumentsDirectory(): string {
    return '/';
  }

  private isInitialized = false;
  private storage: Map<string, any> = new Map();
  private opfsHandle: any = null;

  constructor(
    private path: string,
    private size: number = 10 * 1024 * 1024
  ) {
    // size is used for mobile JSI initialization, on web we track it just for compatibility
    console.log(`SecureDB (Web): Created with size limit ${this.size}`);
  }

  private async ensureInitialized() {
    if (!this.isInitialized) {
      console.log(`SecureDB (Web OPFS): Initializing at ${this.path}`);

      try {
        // Try to use OPFS if available
        const nav = navigator as any;
        if (
          typeof nav !== 'undefined' &&
          nav.storage &&
          nav.storage.getDirectory
        ) {
          const root = await nav.storage.getDirectory();
          const fileName = this.path.split('/').pop() || 'securedb.db';
          this.opfsHandle = await root.getFileHandle(fileName, {
            create: true,
          });
          await this.loadFromOPFS();
        } else {
          console.warn(
            'SecureDB (Web): OPFS not available, falling back to localStorage'
          );
          this.loadFromLocalStorage();
        }
      } catch (e) {
        console.error('SecureDB (Web): Initialization failed', e);
        this.loadFromLocalStorage();
      }

      this.isInitialized = true;
    }
  }

  private async loadFromOPFS() {
    if (!this.opfsHandle) return;
    try {
      const file = await this.opfsHandle.getFile();
      const text = await file.text();
      if (text) {
        const parsed = JSON.parse(text);
        this.storage = new Map(Object.entries(parsed));
      }
    } catch (e) {
      console.warn('SecureDB (Web): Failed to load from OPFS', e);
    }
  }

  private async saveToOPFS() {
    if (!this.opfsHandle) {
      this.saveToLocalStorage();
      return;
    }
    try {
      const writable = await this.opfsHandle.createWritable();
      const obj = Object.fromEntries(this.storage.entries());
      await writable.write(JSON.stringify(obj));
      await writable.close();
    } catch (e) {
      console.warn('SecureDB (Web): Failed to save to OPFS', e);
      this.saveToLocalStorage();
    }
  }

  private loadFromLocalStorage() {
    try {
      const data = localStorage.getItem(`securedb_${this.path}`);
      if (data) {
        const parsed = JSON.parse(data);
        this.storage = new Map(Object.entries(parsed));
      }
    } catch (e) {
      console.warn('SecureDB (Web): Failed to load from localStorage', e);
    }
  }

  private saveToLocalStorage() {
    try {
      const obj = Object.fromEntries(this.storage.entries());
      localStorage.setItem(`securedb_${this.path}`, JSON.stringify(obj));
    } catch (e) {
      console.warn('SecureDB (Web): Failed to save to localStorage', e);
    }
  }

  async set(key: string, value: any): Promise<boolean> {
    await this.ensureInitialized();
    this.storage.set(key, value);
    await this.saveToOPFS();
    return true;
  }

  async get<T = any>(key: string): Promise<T | undefined> {
    await this.ensureInitialized();
    return this.storage.get(key);
  }

  async setMulti(entries: Record<string, any>): Promise<boolean> {
    await this.ensureInitialized();
    for (const [key, value] of Object.entries(entries)) {
      this.storage.set(key, value);
    }
    await this.saveToOPFS();
    return true;
  }

  async getMultiple(keys: string[]): Promise<Record<string, any>> {
    await this.ensureInitialized();
    const result: Record<string, any> = {};
    for (const key of keys) {
      result[key] = this.storage.get(key);
    }
    return result;
  }

  async remove(key: string): Promise<boolean> {
    await this.ensureInitialized();
    const res = this.storage.delete(key);
    await this.saveToOPFS();
    return res;
  }

  async del(key: string): Promise<boolean> {
    return this.remove(key);
  }

  async deleteAll(): Promise<boolean> {
    await this.ensureInitialized();
    this.storage.clear();
    await this.saveToOPFS();
    return true;
  }

  async rangeQuery(
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

  async getAllKeys(): Promise<string[]> {
    await this.ensureInitialized();
    return Array.from(this.storage.keys());
  }

  async clear(): Promise<boolean> {
    await this.deleteAll();
    return true;
  }

  async flush(): Promise<void> {
    await this.saveToOPFS();
  }

  benchmark(): number {
    return 0;
  }
}

export default SecureDB;
