#include <jni.h>
#include <jsi/jsi.h>
#include <vector>
#include <android/log.h>
#include "WALManager.h"
#include "DBEngine.h"
#include "SodiumCryptoContext.h"

#define LOG_TAG "SecureDB_JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace facebook;

static std::vector<uint8_t> getMasterKeyFromJava(JNIEnv *env) {
    LOGI("getMasterKeyFromJava: fetching master key...");
    jclass cls = env->FindClass("com/securedb/KeyStoreManager");
    if (env->ExceptionCheck()) {
        LOGE("Failed to find KeyStoreManager class");
        env->ExceptionClear();
        return std::vector<uint8_t>(32, 0xDB);
    }
    if (!cls) {
        LOGE("KeyStoreManager class is null");
        return std::vector<uint8_t>(32, 0xDB);
    }

    jmethodID mid = env->GetStaticMethodID(cls, "getMasterKey", "()[B");
    if (env->ExceptionCheck()) {
        LOGE("Failed to find getMasterKey method");
        env->ExceptionClear();
        return std::vector<uint8_t>(32, 0xDB);
    }
    if (!mid) {
        LOGE("getMasterKey method ID is null");
        return std::vector<uint8_t>(32, 0xDB);
    }

    jbyteArray keyArray = (jbyteArray)env->CallStaticObjectMethod(cls, mid);
    if (env->ExceptionCheck()) {
        LOGE("Exception while calling getMasterKey");
        env->ExceptionClear();
        return std::vector<uint8_t>(32, 0xDB);
    }
    if (!keyArray) {
        LOGE("getMasterKey returned null");
        return std::vector<uint8_t>(32, 0xDB);
    }

    jsize len = env->GetArrayLength(keyArray);
    std::vector<uint8_t> key(len);
    jbyte* bytes = env->GetByteArrayElements(keyArray, nullptr);
    if (bytes) {
        std::memcpy(key.data(), bytes, len);
        env->ReleaseByteArrayElements(keyArray, bytes, JNI_ABORT);
    } else {
        LOGE("Failed to get byte array elements");
    }

    // Ensure we return exactly 32 bytes for Libsodium
    if (key.size() > 32) key.resize(32);
    if (key.size() < 32) key.insert(key.end(), 32 - key.size(), 0);

    LOGI("getMasterKeyFromJava: successfully fetched %zu bytes", key.size());
    return key;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_securedb_SecureDBModule_nativeInstall(JNIEnv *env, jobject thiz, jlong jsi_runtime_pointer, jint install_mode) {
    LOGI("nativeInstall: starting installation, mode=%d...", install_mode);
    auto runtime = reinterpret_cast<jsi::Runtime *>(jsi_runtime_pointer);
    
    if (runtime) {
        try {
            LOGI("nativeInstall: creating SodiumCryptoContext (Turbo Mode)");
            auto crypto = std::make_unique<secure_db::SodiumCryptoContext>();
            
            std::vector<uint8_t> key = getMasterKeyFromJava(env);
            crypto->setMasterKey(key);
            
            LOGI("nativeInstall: installing Turbo DB Engine");
            secure_db::installDBEngine(*runtime, nullptr, std::move(crypto));
            LOGI("nativeInstall: Turbo Mode installation complete");
        } catch (const std::exception& e) {
            LOGE("nativeInstall error: %s", e.what());
            jclass xcls = env->FindClass("java/lang/RuntimeException");
            if (xcls) env->ThrowNew(xcls, e.what());
        } catch (...) {
            LOGE("nativeInstall unknown error");
            jclass xcls = env->FindClass("java/lang/RuntimeException");
            if (xcls) env->ThrowNew(xcls, "Unknown native exception during SecureDB installation");
        }
    } else {
        LOGE("nativeInstall: runtime pointer is null");
    }
}
