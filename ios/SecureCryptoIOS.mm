#import <Foundation/Foundation.h>
#import <CommonCrypto/CommonCryptor.h>
#import <Security/Security.h>
#include "SecureCryptoIOS.h"
#include <iostream>
#include <cstring>

namespace secure_db {

static const char* kKeychainService = "com.securedb.masterkey";
static const char* kKeychainAccount = "master_key";

std::vector<uint8_t> get_or_create_key() {
    NSDictionary* query = @{
        (id)kSecClass: (id)kSecClassGenericPassword,
        (id)kSecAttrService: [NSString stringWithUTF8String:kKeychainService],
        (id)kSecAttrAccount: [NSString stringWithUTF8String:kKeychainAccount],
        (id)kSecReturnData: @YES
    };

    CFTypeRef result = NULL;
    OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);

    if (status == errSecSuccess) {
        NSData* data = (__bridge_transfer NSData*)result;
        std::vector<uint8_t> key(data.length);
        std::memcpy(key.data(), data.bytes, data.length);
        return key;
    }

    if (status != errSecItemNotFound) {
        return {};
    }

    uint8_t new_key[32];
    status = SecRandomCopyBytes(kSecRandomDefault, 32, new_key);
    if (status != errSecSuccess) {
        return {};
    }

    NSData* key_data = [NSData dataWithBytes:new_key length:32];
    NSDictionary* add_query = @{
        (id)kSecClass: (id)kSecClassGenericPassword,
        (id)kSecAttrService: [NSString stringWithUTF8String:kKeychainService],
        (id)kSecAttrAccount: [NSString stringWithUTF8String:kKeychainAccount],
        (id)kSecValueData: key_data,
        (id)kSecAttrAccessible: (id)kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
    };

    status = SecItemAdd((__bridge CFDictionaryRef)add_query, NULL);
    if (status != errSecSuccess) {
        return {};
    }

    std::vector<uint8_t> key(32);
    std::memcpy(key.data(), new_key, 32);
    return key;
}

void SecureCryptoIOS::loadOrCreateKeyFromKeychain() {
    std::vector<uint8_t> key = get_or_create_key();
    if (key.size() == 32) {
        std::memcpy(key_, key.data(), 32);
    } else {
        std::cerr << "SecureCryptoIOS: Failed to retrieve or create master key from Keychain!\n";
    }
}

void SecureCryptoIOS::generateRandomIV() {
    OSStatus status = SecRandomCopyBytes(kSecRandomDefault, 16, iv_);
    if (status != errSecSuccess) {
        uint8_t default_iv[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                                   0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
        std::memcpy(iv_, default_iv, 16);
    }
}

SecureCryptoIOS::SecureCryptoIOS() {
    loadOrCreateKeyFromKeychain();
    generateRandomIV();
}

SecureCryptoIOS::~SecureCryptoIOS() {
    std::memset(key_, 0, 32);
    std::memset(iv_, 0, 16);
}

std::vector<uint8_t> SecureCryptoIOS::encrypt(const uint8_t* plaintext, size_t length) {
    if (!plaintext || length == 0) {
        return {};
    }

    uint8_t iv[16];
    std::memcpy(iv, iv_, 16);

    size_t outLength = length + kCCBlockSizeAES128;
    std::vector<uint8_t> ciphertext(outLength);
    size_t numBytesEncrypted = 0;

    CCCryptorStatus cryptStatus = CCCrypt(
        kCCEncrypt,
        kCCAlgorithmAES,
        kCCOptionPKCS7Padding,
        key_,
        kCCKeySizeAES256,
        iv,
        plaintext,
        length,
        ciphertext.data(),
        outLength,
        &numBytesEncrypted
    );

    if (cryptStatus == kCCSuccess) {
        ciphertext.resize(numBytesEncrypted);
        return ciphertext;
    }

    return std::vector<uint8_t>();
}

std::vector<uint8_t> SecureCryptoIOS::decrypt(const uint8_t* ciphertext, size_t length) {
    if (!ciphertext || length == 0) {
        return {};
    }

    uint8_t iv[16];
    std::memcpy(iv, iv_, 16);

    size_t outLength = length + kCCBlockSizeAES128;
    std::vector<uint8_t> plaintext(outLength);
    size_t numBytesDecrypted = 0;

    CCCryptorStatus cryptStatus = CCCrypt(
        kCCDecrypt,
        kCCAlgorithmAES,
        kCCOptionPKCS7Padding,
        key_,
        kCCKeySizeAES256,
        iv,
        ciphertext,
        length,
        plaintext.data(),
        outLength,
        &numBytesDecrypted
    );

    if (cryptStatus == kCCSuccess) {
        plaintext.resize(numBytesDecrypted);
        return plaintext;
    }

    return std::vector<uint8_t>();
}

}
