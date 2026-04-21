export enum TurboDBErrorCode {
  OK = 0,
  NOT_INITIALIZED = 1001,
  KEY_TOO_LONG = 1002,
  SERIALIZE_FAIL = 1003,
  DECRYPT_FAIL = 1004,
  CRC_MISMATCH = 1005,
  IO_FAIL = 1006,
  DB_CORRUPT = 1007,
  KEY_NOT_FOUND = 1008,
  INVALID_ARGS = 1009,
  SCHEDULER_STOPPED = 1010,
  REPAIR_FAILED = 1011,
  QUOTA_EXCEEDED = 1012,
}

export class TurboDBError extends Error {
  code: TurboDBErrorCode;

  constructor(code: TurboDBErrorCode, message: string) {
    super(`[TurboDB E${code}] ${message}`);
    this.name = 'TurboDBError';
    this.code = code;
    Object.setPrototypeOf(this, TurboDBError.prototype);
  }
}
