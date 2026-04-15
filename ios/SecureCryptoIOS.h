#pragma once
#include "../cpp/SecureCryptoContext.h"
#include <string>
#include <vector>

namespace secure_db {

class SecureCryptoIOS : public SecureCryptoContext {
public:
    SecureCryptoIOS();
    ~SecureCryptoIOS() override;

    std::vector<uint8_t> encrypt(const uint8_t* plaintext, size_t length) override;
    std::vector<uint8_t> decrypt(const uint8_t* ciphertext, size_t length) override;

private:
    uint8_t key_[32];
    uint8_t iv_[16];

    void loadOrCreateKeyFromKeychain();
    void generateRandomIV();
};

}
