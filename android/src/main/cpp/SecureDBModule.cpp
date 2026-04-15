#include <jni.h>
#include <jsi/jsi.h>
#include "DBEngine.h"
#include "SecureCryptoAndroid.h"

using namespace facebook;

extern "C"
JNIEXPORT void JNICALL
Java_com_securedb_SecureDBModule_nativeInstall(JNIEnv *env, jobject thiz, jlong jsi_runtime_pointer) {
    auto runtime = reinterpret_cast<jsi::Runtime *>(jsi_runtime_pointer);
    
    if (runtime) {
        JavaVM *vm;
        env->GetJavaVM(&vm);
        
        // Create the Android-specific crypto context
        auto crypto = std::make_unique<secure_db::SecureCryptoAndroid>(vm);
        
        // Install the DB Engine into the JSI runtime
        secure_db::installDBEngine(*runtime, std::move(crypto));
    }
}
