package com.securedb;

import androidx.annotation.NonNull;
import com.facebook.react.bridge.ReactApplicationContext;
import com.facebook.react.module.annotations.ReactModule;
import com.facebook.react.turbomodule.core.interfaces.TurboModule;

@ReactModule(name = SecureDBModule.NAME)
public class SecureDBModule extends NativeSecureDBSpec {
    public static final String NAME = "SecureDB";

    public SecureDBModule(ReactApplicationContext reactContext) {
        super(reactContext);
        KeyStoreManager.setContext(reactContext);
    }

    @Override
    @NonNull
    public String getName() {
        return NAME;
    }

    static {
        try {
            System.loadLibrary("sodium");
            System.loadLibrary("react-native-secure-db");
        } catch (UnsatisfiedLinkError e) {
            android.util.Log.e("SecureDB", "Failed to load native libraries", e);
        }
    }

    private native void nativeInstall(long jsiRuntimePointer);

    @Override
    public boolean install() {
        ReactApplicationContext context = getReactApplicationContext();
        if (context != null && context.getJavaScriptContextHolder() != null) {
            long jsiRuntimePointer = context.getJavaScriptContextHolder().get();
            if (jsiRuntimePointer != 0) {
                android.util.Log.i("SecureDB", "Installing JSI Engine with runtime pointer: " + jsiRuntimePointer);
                nativeInstall(jsiRuntimePointer);
                return true;
            } else {
                android.util.Log.e("SecureDB", "JSI Runtime pointer is null (0)");
            }
        } else {
            android.util.Log.e("SecureDB", "ReactContext or JavaScriptContextHolder is null");
        }
        return false;
    }

    @Override
    public String getDocumentsDirectory() {
        return getReactApplicationContext().getFilesDir().getAbsolutePath();
    }

    @Override
    public String getVersion() {
        return "1.0.0";
    }
}
