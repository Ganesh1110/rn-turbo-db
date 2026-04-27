package com.turbodb;

import android.util.Log;
import com.turbodb.NativeTurboDBSpec;
import com.facebook.react.bridge.ReactApplicationContext;
import java.io.File;

public class TurboDBModule extends NativeTurboDBSpec {
  public static final String NAME = "TurboDB";
  private static final String TAG = "TurboDB";

  private static boolean libraryLoaded = false;
  private static String libraryLoadError = null;

  static {
    try {
      System.loadLibrary("react-native-turbo-db");
      libraryLoaded = true;
      Log.i(TAG, "Native library loaded successfully");
    } catch (UnsatisfiedLinkError e) {
      libraryLoaded = false;
      libraryLoadError = e.getMessage();
      Log.e(TAG, "Failed to load native library: " + e.getMessage());
    } catch (Exception e) {
      libraryLoaded = false;
      libraryLoadError = e.getMessage();
      Log.e(TAG, "Unexpected error loading library: " + e.getMessage());
    }
  }

  public static boolean isLibraryLoaded() {
    return libraryLoaded;
  }

  public static String getLibraryLoadError() {
    return libraryLoadError;
  }

  public TurboDBModule(ReactApplicationContext reactContext) {
    super(reactContext);
    if (!libraryLoaded) {
      throw new RuntimeException(
        "[TurboDB] Native library not loaded. Cannot initialize TurboDBModule. " +
        "Error: " + libraryLoadError +
        ". Make sure the native build completed successfully.");
    }
    KeyStoreManager.init(reactContext);
  }

  @Override
  public String getName() {
    return NAME;
  }

  @Override
  public boolean install() {
    if (!libraryLoaded) {
      Log.e(TAG, "install() failed: library not loaded");
      return false;
    }

    long jsiRuntimePointer = getReactApplicationContext().getJavaScriptContextHolder().get();
    if (jsiRuntimePointer == 0) {
       Log.e(TAG, "install() failed: JSI runtime pointer is 0");
       return false;
    }

    try {
      nativeInstall(jsiRuntimePointer, 1);
      Log.i(TAG, "install() - nativeInstall successful");
      return true;
    } catch (Exception e) {
      Log.e(TAG, "install() failed: " + e.getMessage());
      return false;
    }
  }

  @Override
  public String getDocumentsDirectory() {
    File docsDir = getReactApplicationContext().getFilesDir();
    return docsDir.getAbsolutePath();
  }

  @Override
  public boolean isInitialized() {
    return isInitializedNative();
  }
  
  public String getVersion() {
    return "0.1.0";
  }

  private native void nativeInstall(long jsiRuntimePointer, int installMode);
  private static native boolean isInitializedNative();
}
