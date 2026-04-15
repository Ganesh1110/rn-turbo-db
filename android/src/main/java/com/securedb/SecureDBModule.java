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
    }

    @Override
    @NonNull
    public String getName() {
        return NAME;
    }

    static {
        System.loadLibrary("react-native-secure-db");
    }

    private native void nativeInstall(long jsiRuntimePointer);

    @Override
    public void install() {
        long jsiRuntimePointer = getReactApplicationContext().getJavaScriptContextHolder().get();
        nativeInstall(jsiRuntimePointer);
    }

    @Override
    public String getVersion() {
        return "1.0.0";
    }
}
