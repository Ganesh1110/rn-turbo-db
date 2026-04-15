package com.securedb;

import androidx.annotation.Nullable;
import com.facebook.react.bridge.NativeModule;
import com.facebook.react.bridge.ReactApplicationContext;
import com.facebook.react.module.model.ReactModuleInfo;
import com.facebook.react.module.model.ReactModuleInfoProvider;
import com.facebook.react.TurboReactPackage;

import java.util.HashMap;
import java.util.Map;

public class SecureDBPackage extends TurboReactPackage {

    @Nullable
    @Override
    public NativeModule getModule(String name, ReactApplicationContext reactContext) {
        if (name.equals(SecureDBModule.NAME)) {
            return new SecureDBModule(reactContext);
        } else {
            return null;
        }
    }

    @Override
    public ReactModuleInfoProvider getReactModuleInfoProvider() {
        return () -> {
            final Map<String, ReactModuleInfo> moduleInfos = new HashMap<>();
            boolean isTurboModule = true;
            moduleInfos.put(
                    SecureDBModule.NAME,
                    new ReactModuleInfo(
                            SecureDBModule.NAME,
                            SecureDBModule.NAME,
                            false, // canOverrideExistingModule
                            false, // needsEagerInit
                            true, // hasConstants
                            false, // isCxxModule
                            isTurboModule // isTurboModule
                    ));
            return moduleInfos;
        };
    }
}
