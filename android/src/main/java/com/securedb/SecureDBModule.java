package com.securedb;

import com.facebook.fbreact.specs.NativeSecureDBSpec;
import com.facebook.react.bridge.ReactApplicationContext;
import java.io.File;

public class SecureDBModule extends NativeSecureDBSpec {
  static {
    System.loadLibrary("react-native-secure-db");
  }

  public SecureDBModule(ReactApplicationContext reactContext) {
    super(reactContext);
    KeyStoreManager.init(reactContext);
  }

  @Override
  public void install() {
    long jsiRuntimePointer = getReactApplicationContext().getJavaScriptContextHolder().get();
    if (jsiRuntimePointer != 0) {
      nativeInstall(jsiRuntimePointer, 1);
    }
  }

  @Override
  public String getDocumentsDirectory() {
    File docsDir = getReactApplicationContext().getFilesDir();
    return docsDir.getAbsolutePath();
  }

  @Override
  public String getVersion() {
    return "0.1.0";
  }

  private native void nativeInstall(long jsiRuntimePointer, int installMode);
}
