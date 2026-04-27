#include <jni.h>
#include <jsi/jsi.h>
#include <vector>
#include <android/log.h>
#include <memory>
#include <string>
#include "WALManager.h"
#include "DBEngine.h"
#include "SodiumCryptoContext.h"

#define LOG_TAG "TurboDB_JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace facebook;

namespace {
    std::shared_ptr<turbo_db::DBEngine> g_dbEngine;
    std::unique_ptr<turbo_db::SodiumCryptoContext> g_crypto;
}

static std::vector<uint8_t> getMasterKeyFromJava(JNIEnv *env) {
    jclass cls = env->FindClass("com/turbodb/KeyStoreManager");
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGE("getMasterKeyFromJava: KeyStoreManager class not found");
        return std::vector<uint8_t>(32, 0xDB);
    }
    
    jmethodID mid = env->GetStaticMethodID(cls, "getMasterKey", "()[B");
    if (env->ExceptionCheck() || !mid) {
        env->ExceptionClear();
        LOGE("getMasterKeyFromJava: getMasterKey method not found");
        return std::vector<uint8_t>(32, 0xDB);
    }
    
    jbyteArray keyArray = (jbyteArray)env->CallStaticObjectMethod(cls, mid);
    if (env->ExceptionCheck() || !keyArray) {
        env->ExceptionClear();
        LOGE("getMasterKeyFromJava: getMasterKey call failed");
        return std::vector<uint8_t>(32, 0xDB);
    }
    
    jsize len = env->GetArrayLength(keyArray);
    std::vector<uint8_t> key(len);
    jbyte* bytes = env->GetByteArrayElements(keyArray, nullptr);
    if (bytes) {
        std::memcpy(key.data(), bytes, len);
        env->ReleaseByteArrayElements(keyArray, bytes, JNI_ABORT);
    }
    
    if (key.size() > 32) key.resize(32);
    if (key.size() < 32) key.insert(key.end(), 32 - key.size(), 0);
    
    return key;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_turbodb_TurboDBModule_nativeInstall(JNIEnv *env, jobject thiz, jlong jsi_runtime_pointer, jint install_mode) {

    LOGI("nativeInstall: starting, pointer=%lld", (long long)jsi_runtime_pointer);

    if (jsi_runtime_pointer == 0) {
        LOGE("nativeInstall: pointer is NULL!");
        return;
    }

    auto runtime = reinterpret_cast<jsi::Runtime *>(jsi_runtime_pointer);
    if (!runtime) {
        LOGE("nativeInstall: failed to cast pointer");
        return;
    }
    
    try {
        if (!g_dbEngine) {
            LOGI("nativeInstall: creating new DBEngine instance");
            g_crypto = std::make_unique<turbo_db::SodiumCryptoContext>();
            std::vector<uint8_t> key = getMasterKeyFromJava(env);
            g_crypto->setMasterKey(key);
            g_dbEngine = std::make_shared<turbo_db::DBEngine>(nullptr, std::move(g_crypto));
        } else {
            LOGI("nativeInstall: reusing existing DBEngine instance");
        }
        
        jsi::Object nativeDBObj = jsi::Object::createFromHostObject(*runtime, g_dbEngine);
        
        // Attach to global for standard RN
        runtime->global().setProperty(*runtime, "NativeDB", nativeDBObj);
        
        // Redundant attachment for Bridgeless/Fabric compatibility
        runtime->global().setProperty(*runtime, "__NativeDB", nativeDBObj);
        
        LOGI("nativeInstall: success - set NativeDB on global");
    } catch (const std::exception& e) {
        LOGE("nativeInstall error: %s", e.what());
    } catch (...) {
        LOGE("nativeInstall unknown error");
    }
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_turbodb_TurboDBModule_isInitializedNative(JNIEnv *env, jobject thiz) {
    return g_dbEngine != nullptr ? JNI_TRUE : JNI_FALSE;
}

JavaVM* g_jvm = nullptr;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}