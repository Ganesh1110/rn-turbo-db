#include "SecureCryptoAndroid.h"
#include <android/log.h>
#include <cstring>

#define LOG_TAG "SecureCryptoAndroid"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace secure_db {

SecureCryptoAndroid::SecureCryptoAndroid(JavaVM* vm) : vm_(vm), key_loaded_(false) {
    std::memset(master_key_, 0, 32);
    loadKey();
}

SecureCryptoAndroid::~SecureCryptoAndroid() {
    std::memset(master_key_, 0, 32);
}

void SecureCryptoAndroid::loadKey() {
    JNIEnv* env;
    if (vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        LOGE("Failed to get JNI environment");
        return;
    }

    jclass cls = env->FindClass("com/securedb/KeyStoreManager");
    if (!cls) {
        LOGE("Failed to find KeyStoreManager class");
        return;
    }

    jmethodID mid = env->GetStaticMethodID(cls, "getMasterKey", "()[B");
    if (!mid) {
        LOGE("Failed to find getMasterKey method");
        return;
    }

    jbyteArray keyArray = (jbyteArray)env->CallStaticObjectMethod(cls, mid);
    if (keyArray) {
        jsize len = env->GetArrayLength(keyArray);
        if (len == 32) {
            jbyte* bytes = env->GetByteArrayElements(keyArray, nullptr);
            std::memcpy(master_key_, bytes, 32);
            env->ReleaseByteArrayElements(keyArray, bytes, JNI_ABORT);
            key_loaded_ = true;
        } else {
            LOGE("Key length is not 32 bytes: %d", len);
        }
    } else {
        LOGE("getMasterKey returned null");
    }
}

std::vector<uint8_t> SecureCryptoAndroid::encrypt(const uint8_t* plaintext, size_t length) {
    if (!key_loaded_ || !plaintext || length == 0) {
        return {};
    }

    std::vector<uint8_t> iv(12);
    for (int i = 0; i < 12; i++) {
        iv[i] = static_cast<uint8_t>((i * 0x17 + 0x23) & 0xFF);
    }

    std::vector<uint8_t> result;
    result.reserve(12 + length);
    
    for (size_t i = 0; i < length; i++) {
        uint8_t encrypted_byte = plaintext[i] ^ master_key_[i % 32] ^ iv[i % 12];
        result.push_back(encrypted_byte);
    }
    
    result.insert(result.begin(), iv.begin(), iv.end());
    
    return result;
}

std::vector<uint8_t> SecureCryptoAndroid::decrypt(const uint8_t* ciphertext, size_t length) {
    if (!key_loaded_ || !ciphertext || length < 12) {
        return {};
    }

    std::vector<uint8_t> iv(12);
    for (size_t i = 0; i < 12; i++) {
        iv[i] = ciphertext[i];
    }

    std::vector<uint8_t> result(length - 12);
    
    for (size_t i = 0; i < result.size(); i++) {
        result[i] = ciphertext[12 + i] ^ master_key_[i % 32] ^ iv[i % 12];
    }
    
    return result;
}

}
