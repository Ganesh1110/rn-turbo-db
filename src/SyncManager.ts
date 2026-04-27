import type { TurboDB } from './index';

export interface SyncRecord {
  key: string;
  data: any; // The deserialized JSON object
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

export type SyncEvent =
  | 'started'
  | 'pull_success'
  | 'push_success'
  | 'error'
  | 'stopped'
  | 'paused'
  | 'resumed'
  | 'network_status_change';

/**
 * Orchestrates Offline-First Synchronization for TurboDB.
 * Handles background push/pull cycles, network coordination, and retry backoff.
 */
export class SyncManager {
  private db: TurboDB;
  private adapter: SyncAdapter;
  private isSyncing = false;
  private isPaused = false;
  private isOnline = true;
  private syncTimer: any | null = null;
  private options: SyncOptions;

  // Local persistence for sync cursors
  private readonly CURSOR_KEY = '__sys_sync_cursor';
  private lastRemoteVersion: number = 0;
  private lastPushedClock: number = 0;

  private listeners: Set<(event: SyncEvent, data?: any) => void> = new Set();

  constructor(db: TurboDB, adapter: SyncAdapter, options: SyncOptions = {}) {
    this.db = db;
    this.adapter = adapter;
    this.options = {
      autoSync: false,
      syncIntervalMs: 60000,
      ...options,
    };
  }

  /**
   * Start the background sync loop.
   */
  async start(): Promise<void> {
    // Allow re-starting after a previous stop()
    this.isPaused = false;
    try {
      const state = await this.db.getAsync(this.CURSOR_KEY);
      if (state && typeof state.lastRemoteVersion === 'number') {
        this.lastRemoteVersion = state.lastRemoteVersion;
      }
      if (state && typeof state.lastPushedClock === 'number') {
        this.lastPushedClock = state.lastPushedClock;
      }
    } catch {
      // Ignored: first time
    }

    if (this.options.autoSync && !this.isPaused) {
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
    // Set isPaused so that any in-flight forceSync() finally-block
    // does NOT re-schedule via scheduleNextSync().
    this.isPaused = true;
    this.notify('stopped');
  }

  pause(): void {
    this.isPaused = true;
    if (this.syncTimer) clearTimeout(this.syncTimer);
    this.notify('paused');
  }

  resume(): void {
    this.isPaused = false;
    this.notify('resumed');
    if (this.options.autoSync) {
      this.forceSync();
    }
  }

  setNetworkStatus(online: boolean): void {
    const changed = this.isOnline !== online;
    this.isOnline = online;
    if (changed) {
      this.notify('network_status_change', { online });
      if (online && this.options.autoSync && !this.isPaused) {
        this.forceSync();
      }
    }
  }

  /**
   * Manually trigger a full pull + push sync cycle.
   */
  async forceSync(): Promise<void> {
    if (this.isSyncing || this.isPaused || !this.isOnline) return;
    this.isSyncing = true;
    this.notify('started');

    try {
      // ── Step 1: Pull Remote Changes ──
      const pullResult = await this.adapter.pullChanges(this.lastRemoteVersion);
      if (pullResult.changes.length > 0) {
        await this.db.applyRemoteChangesAsync(pullResult.changes);
        this.lastRemoteVersion = pullResult.latest_remote_version;
        await this.db.setAsync(this.CURSOR_KEY, {
          lastRemoteVersion: this.lastRemoteVersion,
          lastPushedClock: this.lastPushedClock,
        });
      }
      this.notify('pull_success', { count: pullResult.changes.length });

      // ── Step 2: Push Local Changes ──
      // Use lastPushedClock (not 0) so we only push NEW changes since the last sync
      const localChanges: SyncChanges = await this.db.getLocalChangesAsync(
        this.lastPushedClock
      );
      if (localChanges.changes.length > 0) {
        const acks = await this.adapter.pushChanges(localChanges.changes);
        await this.db.markPushedAsync(acks);
        // Advance cursor so next sync only pushes new changes
        this.lastPushedClock = localChanges.latest_clock;
        await this.db.setAsync(this.CURSOR_KEY, {
          lastRemoteVersion: this.lastRemoteVersion,
          lastPushedClock: this.lastPushedClock,
        });
      }
      this.notify('push_success', { count: localChanges.changes.length });
    } catch (error: any) {
      this.notify('error', error);
    } finally {
      this.isSyncing = false;
      if (this.options.autoSync && !this.isPaused) {
        this.scheduleNextSync();
      }
    }
  }

  onSyncEvent(callback: (event: SyncEvent, data?: any) => void): () => void {
    this.listeners.add(callback);
    return () => this.listeners.delete(callback);
  }

  private notify(event: SyncEvent, data?: any) {
    for (const listener of this.listeners) {
      try {
        listener(event, data);
      } catch (e) {
        console.error('SyncEvent listener error:', e);
      }
    }
  }

  private scheduleNextSync() {
    if (this.syncTimer) clearTimeout(this.syncTimer);
    if (this.isPaused || !this.isOnline) return;

    this.syncTimer = setTimeout(() => {
      this.forceSync();
    }, this.options.syncIntervalMs);
  }
}
