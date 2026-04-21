import { TurboDB } from '../index';

describe('TurboDB Integration Tests', () => {
  let db: TurboDB;
  const DB_PATH = 'test_db';

  beforeEach(async () => {
    db = await TurboDB.create(DB_PATH, 1024 * 1024);
    await db.deleteAll();
  });

  afterEach(async () => {
    await db.deleteAll();
  });

  test('Persistence: set -> restart -> get', async () => {
    await db.setAsync('persist_key', { hello: 'world' });

    // Simulate restart by creating a new instance
    const db2 = await TurboDB.create(DB_PATH, 1024 * 1024);
    const val = await db2.getAsync('persist_key');
    expect(val).toEqual({ hello: 'world' });
  });

  test('Async: ensure all promises resolve', async () => {
    const p1 = db.setAsync('a', 1);
    const p2 = db.getAsync('a');
    const p3 = db.removeAsync('a');

    const [s, g, r] = await Promise.all([p1, p2, p3]);
    expect(s).toBe(true);
    expect(g).toBe(1);
    expect(r).toBe(true);
  });

  test('Concurrency: parallel writes', async () => {
    const ops = [];
    for (let i = 0; i < 100; i++) {
      ops.push(db.setAsync(`key_${i}`, i));
    }
    await Promise.all(ops);

    const keys = await db.getAllKeysAsync();
    expect(keys.length).toBe(100);
  });

  test('Scale: 1000 records test', async () => {
    const entries: Record<string, number> = {};
    for (let i = 0; i < 1000; i++) {
      entries[`scale_${i}`] = i;
    }

    await db.setMultiAsync(entries);
    const keys = await db.getAllKeysAsync();
    expect(keys.length).toBe(1000);

    const val = await db.getAsync('scale_500');
    expect(val).toBe(500);
  });

  test('Deletion: ensure deleted keys are removed from index', async () => {
    await db.setAsync('del_test', 123);
    let keys = await db.getAllKeysAsync();
    expect(keys).toContain('del_test');

    await db.removeAsync('del_test');
    keys = await db.getAllKeysAsync();
    expect(keys).not.toContain('del_test');

    const val = await db.getAsync('del_test');
    expect(val).toBeUndefined();
  });
});
