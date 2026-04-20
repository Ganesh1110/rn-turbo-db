import type { TurboDB } from './index';

export interface SyncRecord {
  key: string;
  data: any;       // The deserialized JSON object
  logical_clock: number;
  remote_version: number;
  updated_at: number;
  is_deleted: boolean;
}

export interface SyncChanges {
  latest_clock: number;
  changes: SyncRecord[];
}

export interface SyncAck {
  key: string;
  remote_version: number;
}

export interface SyncAdapter {
  pullChanges: (lastRemoteVersion: number) => Promise<{
    changes: SyncRecord[];
    latest_remote_version: number;
  }>;
  pushChanges: (changes: SyncRecord[]) => Promise<SyncAck[]>;
}

export interface SyncOptions {
  autoSync?: boolean;
  syncIntervalMs?: number; // default: 60000
}

export type SyncEvent = 'started' | 'pull_success' | 'push_success' | 'error' | 'stopped';

/**
 * Orchestrates Offline-First Synchronization for TurboDB.
 * Handles background push/pull cycles, network coordination, and retry backoff.
 */
export class SyncManager {
  private db: TurboDB;
  private adapter: SyncAdapter;
  private isSyncing = false;
  private syncTimer: any | null = null;
  private options: SyncOptions;
  
  // Local persistence for sync cursors
  private readonly CURSOR_KEY = '__sys_sync_cursor';
  private lastRemoteVersion: number = 0;
  
  private listeners: Set<(event: SyncEvent, error?: Error) => void> = new Set();

  constructor(db: TurboDB, adapter: SyncAdapter, options: SyncOptions = {}) {
    this.db = db;
    this.adapter = adapter;
    this.options = {
      autoSync: false,
      syncIntervalMs: 60000,
      ...options
    };
  }

  /**
   * Start the background sync loop.
   */
  async start(): Promise<void> {
    // Load last sync state
    try {
      const state = await this.db.getAsync(this.CURSOR_KEY);
      if (state && typeof state.lastRemoteVersion === 'number') {
        this.lastRemoteVersion = state.lastRemoteVersion;
      }
    } catch (e) {
      // Ignored: first time
    }

    if (this.options.autoSync) {
      this.scheduleNextSync();
    }
  }

  /**
   * Stop the background sync loop.
   */
  stop(): void {
    if (this.syncTimer) {
      clearTimeout(this.syncTimer);
      this.syncTimer = null;
    }
    this.notify('stopped');
  }

  /**
   * Manually trigger a full pull + push sync cycle.
   */
  async forceSync(): Promise<void> {
    if (this.isSyncing) return;
    this.isSyncing = true;
    this.notify('started');

    try {
      // ── Step 1: Pull Remote Changes ──
      const pullResult = await this.adapter.pullChanges(this.lastRemoteVersion);
      if (pullResult.changes.length > 0) {
        // Apply remote changes utilizing C++ LWW conflict resolver
        // @ts-ignore - internal async API
        await this.db.applyRemoteChangesAsync(pullResult.changes);
        
        this.lastRemoteVersion = pullResult.latest_remote_version;
        await this.db.setAsync(this.CURSOR_KEY, { lastRemoteVersion: this.lastRemoteVersion });
      }
      this.notify('pull_success');

      // ── Step 2: Push Local Changes ──
      // @ts-ignore - internal async API
      const localChanges: SyncChanges = await this.db.getLocalChangesAsync(0); // Fetch all dirty
      
      if (localChanges.changes.length > 0) {
        const acks = await this.adapter.pushChanges(localChanges.changes);
        
        // Mark as pushed to clear dirty flag and bump remote_version
        // @ts-ignore - internal async API
        await this.db.markPushedAsync(acks);
      }
      this.notify('push_success');

    } catch (error: any) {
      this.notify('error', error);
    } finally {
      this.isSyncing = false;
      if (this.options.autoSync) {
        this.scheduleNextSync();
      }
    }
  }

  onSyncEvent(callback: (event: SyncEvent, error?: Error) => void): () => void {
    this.listeners.add(callback);
    return () => this.listeners.delete(callback);
  }

  private notify(event: SyncEvent, error?: Error) {
    for (const listener of this.listeners) {
      try {
        listener(event, error);
      } catch (e) {
        console.error('SyncEvent listener threw an error:', e);
      }
    }
  }

  private scheduleNextSync() {
    if (this.syncTimer) clearTimeout(this.syncTimer);
    this.syncTimer = setTimeout(() => {
      this.forceSync();
    }, this.options.syncIntervalMs);
  }
}
