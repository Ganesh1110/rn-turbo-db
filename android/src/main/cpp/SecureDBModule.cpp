#include <jni.h>
#include <jsi/jsi.h>
#include <vector>
#include "WALManager.h"
#include "DBEngine.h"
#include "SodiumCryptoContext.h"

using namespace facebook;

static std::vector<uint8_t> getMasterKeyFromJava(JNIEnv *env) {
    jclass cls = env->FindClass("com/securedb/KeyStoreManager");
    if (!cls) return std::vector<uint8_t>(32, 0);

    jmethodID mid = env->GetStaticMethodID(cls, "getMasterKey", "()[B");
    if (!mid) return std::vector<uint8_t>(32, 0);

    jbyteArray keyArray = (jbyteArray)env->CallStaticObjectMethod(cls, mid);
    if (!keyArray) return std::vector<uint8_t>(32, 0);

    jsize len = env->GetArrayLength(keyArray);
    std::vector<uint8_t> key(len);
    jbyte* bytes = env->GetByteArrayElements(keyArray, nullptr);
    std::memcpy(key.data(), bytes, len);
    env->ReleaseByteArrayElements(keyArray, bytes, JNI_ABORT);

    // Ensure we return exactly 32 bytes for Libsodium
    if (key.size() > 32) key.resize(32);
    if (key.size() < 32) key.insert(key.end(), 32 - key.size(), 0);

    return key;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_securedb_SecureDBModule_nativeInstall(JNIEnv *env, jobject thiz, jlong jsi_runtime_pointer) {
    auto runtime = reinterpret_cast<jsi::Runtime *>(jsi_runtime_pointer);
    
    if (runtime) {
        try {
            JavaVM *vm;
            env->GetJavaVM(&vm);
            
            // Create the unified Libsodium-based crypto context
            auto crypto = std::make_unique<secure_db::SodiumCryptoContext>();
            crypto->setMasterKey(getMasterKeyFromJava(env));
            
            // Install the DB Engine into the JSI runtime
            secure_db::installDBEngine(*runtime, std::move(crypto));
        } catch (const std::exception& e) {
            jclass xcls = env->FindClass("java/lang/RuntimeException");
            if (xcls) env->ThrowNew(xcls, e.what());
        }
    }
}
