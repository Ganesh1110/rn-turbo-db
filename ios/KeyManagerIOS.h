#pragma once

#include <string>
#include <vector>

namespace secure_db {

class KeyManagerIOS {
public:
    static std::vector<uint8_t> getOrGenerateMasterKey(const std::string& alias);
    static bool deleteMasterKey(const std::string& alias);

private:
    static SecKeyRef getSecureEnclaveKey();
    static std::vector<uint8_t> wrapMasterKey(const std::vector<uint8_t>& masterKey, SecKeyRef publicKey);
    static std::vector<uint8_t> unwrapMasterKey(const std::vector<uint8_t>& wrappedKey, SecKeyRef privateKey);
};

}