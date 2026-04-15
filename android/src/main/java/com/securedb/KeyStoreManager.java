package com.securedb;

import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import java.security.KeyStore;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import java.security.SecureRandom;
import android.util.Base64;
import java.io.ByteArrayOutputStream;

public class KeyStoreManager {
    private static final String KEY_ALIAS = "com.securedb.masterkey";
    private static final String ANDROID_KEYSTORE = "AndroidKeyStore";
    private static final int GCM_IV_LENGTH = 12;
    private static final int GCM_TAG_LENGTH = 128;

    private static byte[] cachedKey = null;

    public static byte[] getMasterKey() {
        if (cachedKey != null) {
            return cachedKey;
        }

        try {
            KeyStore keyStore = KeyStore.getInstance(ANDROID_KEYSTORE);
            keyStore.load(null);

            if (!keyStore.containsAlias(KEY_ALIAS)) {
                KeyGenerator keyGenerator = KeyGenerator.getInstance(
                        KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEYSTORE);
                
                keyGenerator.init(new KeyGenParameterSpec.Builder(
                        KEY_ALIAS,
                        KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                        .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                        .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                        .setKeySize(256)
                        .setUserAuthenticationRequired(false)
                        .build());
                
                keyGenerator.generateKey();
            }

            SecretKey secretKey = (SecretKey) keyStore.getKey(KEY_ALIAS, null);
            cachedKey = secretKey.getEncoded();
            
            if (cachedKey == null) {
                cachedKey = generateAndStoreDerivedKey(keyStore, secretKey);
            }
            
            return cachedKey;
        } catch (Exception e) {
            e.printStackTrace();
            return null;
        }
    }

    private static byte[] generateAndStoreDerivedKey(KeyStore keyStore, SecretKey hardwareKey) {
        try {
            byte[] derivedKey = new byte[32];
            new SecureRandom().nextBytes(derivedKey);

            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            GCMParameterSpec parameterSpec = new GCMParameterSpec(GCM_TAG_LENGTH, new byte[GCM_IV_LENGTH]);
            cipher.init(Cipher.ENCRYPT_MODE, hardwareKey, parameterSpec);
            byte[] encryptedKey = cipher.doFinal(derivedKey);

            java.io.FileOutputStream fos = new java.io.FileOutputStream(
                    new java.io.File(new android.content.ContextWrapper(null).getFilesDir(), "securedb.key"));
            fos.write(encryptedKey);
            fos.close();

            return derivedKey;
        } catch (Exception e) {
            e.printStackTrace();
            return null;
        }
    }

    public static byte[] encrypt(byte[] plaintext) {
        try {
            byte[] key = getMasterKey();
            if (key == null) return null;

            javax.crypto.spec.SecretKeySpec keySpec = new javax.crypto.spec.SecretKeySpec(key, "AES");
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            
            byte[] iv = new byte[GCM_IV_LENGTH];
            new SecureRandom().nextBytes(iv);
            
            GCMParameterSpec gcmSpec = new GCMParameterSpec(GCM_TAG_LENGTH, iv);
            cipher.init(Cipher.ENCRYPT_MODE, keySpec, gcmSpec);
            
            byte[] ciphertext = cipher.doFinal(plaintext);

            ByteArrayOutputStream outputStream = new ByteArrayOutputStream();
            outputStream.write(iv);
            outputStream.write(ciphertext);
            return outputStream.toByteArray();
        } catch (Exception e) {
            e.printStackTrace();
            return null;
        }
    }

    public static byte[] decrypt(byte[] encryptedData) {
        try {
            byte[] key = getMasterKey();
            if (key == null) return null;

            ByteArrayInputStream inputStream = new ByteArrayInputStream(encryptedData);
            byte[] iv = new byte[GCM_IV_LENGTH];
            inputStream.read(iv);
            
            byte[] ciphertext = new byte[inputStream.available()];
            inputStream.read(ciphertext);

            javax.crypto.spec.SecretKeySpec keySpec = new javax.crypto.spec.SecretKeySpec(key, "AES");
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            GCMParameterSpec gcmSpec = new GCMParameterSpec(GCM_TAG_LENGTH, iv);
            cipher.init(Cipher.DECRYPT_MODE, keySpec, gcmSpec);
            
            return cipher.doFinal(ciphertext);
        } catch (Exception e) {
            e.printStackTrace();
            return null;
        }
    }
}
