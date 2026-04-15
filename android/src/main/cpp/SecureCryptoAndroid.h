#pragma once
#include "SecureCryptoContext.h"
#include <jni.h>
#include <vector>

namespace secure_db {

class SecureCryptoAndroid : public SecureCryptoContext {
public:
    explicit SecureCryptoAndroid(JavaVM* vm);
    ~SecureCryptoAndroid() override;

    std::vector<uint8_t> encrypt(const uint8_t* plaintext, size_t length) override;
    std::vector<uint8_t> decrypt(const uint8_t* ciphertext, size_t length) override;

private:
    JavaVM* vm_;
    uint8_t master_key_[32];
    bool key_loaded_ = false;

    void loadKey();
};

}
