import { SecureDB } from '../index';

// Mock the native module and JSI global
jest.mock('../NativeSecureDB', () => ({
  default: {
    install: jest.fn(),
  },
}));

(global as any).NativeDB = {
  initStorage: jest.fn(() => true),
  insertRec: jest.fn(() => true),
  findRec: jest.fn((key: string) => {
    if (key === 'test_key') return { data: 'test_value' };
    return undefined;
  }),
  clearStorage: jest.fn(() => true),
};

describe('SecureDB', () => {
  let db: SecureDB;

  beforeEach(() => {
    db = new SecureDB('/tmp/test.db');
    jest.clearAllMocks();
  });

  it('initializes on first set', () => {
    db.set('foo', 'bar');
    expect((global as any).NativeDB.initStorage).toHaveBeenCalled();
  });

  it('can set and get a value', () => {
    const success = db.set('test_key', { data: 'test_value' });
    expect(success).toBe(true);
    
    const val = db.get('test_key');
    expect(val).toEqual({ data: 'test_value' });
  });

  it('returns undefined for non-existent key', () => {
    const val = db.get('missing');
    expect(val).toBeUndefined();
  });

  it('can clear storage', () => {
    const success = db.clear();
    expect(success).toBe(true);
    expect((global as any).NativeDB.clearStorage).toHaveBeenCalled();
  });
});
