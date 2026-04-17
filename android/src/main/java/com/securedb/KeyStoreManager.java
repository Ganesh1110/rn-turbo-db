package com.securedb;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Base64;
import java.security.SecureRandom;

public class KeyStoreManager {
  private static final String PREF_NAME = "SecureDBPrefs";
  private static final String KEY_MASTER_KEY = "master_key";

  private static Context context;

  public static void init(Context ctx) {
    context = ctx.getApplicationContext();
  }

  public static byte[] getMasterKey() {
    if (context == null) {
      return new byte[32];
    }

    SharedPreferences prefs = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE);
    String encodedKey = prefs.getString(KEY_MASTER_KEY, null);

    if (encodedKey == null) {
      byte[] key = new byte[32];
      new SecureRandom().nextBytes(key);
      encodedKey = Base64.encodeToString(key, Base64.DEFAULT);
      prefs.edit().putString(KEY_MASTER_KEY, encodedKey).apply();
      return key;
    }

    return Base64.decode(encodedKey, Base64.DEFAULT);
  }
}
