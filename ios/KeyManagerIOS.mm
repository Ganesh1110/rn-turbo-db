#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include "KeyManagerIOS.h"
#include <stdexcept>
#include "CommonCrypto/CommonRandom.h"

namespace secure_db {

static NSString* kWrappedKeyTag = @"com.securedb.wrappedkey";
static NSString* kSecureEnclaveTag = @"com.securedb.enclave.key";

static SecKeyRef generateSecureEnclaveKey() {
    CFErrorRef error = NULL;

    SecAccessControlRef accessControl = SecAccessControlCreateWithFlags(
        kCFAllocatorDefault,
        kSecAttrAccessibleWhenUnlockedThisDeviceOnly,
        kSecAccessControlPrivateKeyUsage,
        &error
    );

    if (!accessControl) {
        if (error) CFRelease(error);
        throw std::runtime_error("Failed to create access control");
    }

    NSDictionary *attributes = @{
        (__bridge id)kSecAttrKeyType: (__bridge id)kSecAttrKeyTypeECSECPrimeRandom,
        (__bridge id)kSecAttrKeySizeInBits: @256,
        (__bridge id)kSecAttrTokenID: (__bridge id)kSecAttrTokenIDSecureEnclave,
        (__bridge id)kSecPrivateKeyAttrs: @{
            (__bridge id)kSecAttrIsPermanent: @YES,
            (__bridge id)kSecAttrApplicationTag: [kSecureEnclaveTag dataUsingEncoding:NSUTF8StringEncoding],
            (__bridge id)kSecAttrAccessControl: (__bridge id)accessControl
        }
    };

    SecKeyRef privateKey = SecKeyCreateRandomKey((__bridge CFDictionaryRef)attributes, &error);
    CFRelease(accessControl);

    if (!privateKey) {
        if (error) CFRelease(error);
        throw std::runtime_error("Failed to generate Secure Enclave key");
    }

    return privateKey;
}

static SecKeyRef getOrCreateSecureEnclaveKey() {
    NSData *tag = [kSecureEnclaveTag dataUsingEncoding:NSUTF8StringEncoding];
    NSDictionary *query = @{
        (__bridge id)kSecClass: (__bridge id)kSecClassKey,
        (__bridge id)kSecAttrApplicationTag: tag,
        (__bridge id)kSecAttrKeyType: (__bridge id)kSecAttrKeyTypeECSECPrimeRandom,
        (__bridge id)kSecReturnRef: @YES
    };

    SecKeyRef existingKey = NULL;
    OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, (CFTypeRef*)&existingKey);

    if (status == errSecSuccess && existingKey) {
        return existingKey;
    }

    if (existingKey) {
        CFRelease(existingKey);
    }

    return generateSecureEnclaveKey();
}

static std::vector<uint8_t> secKeyToVector(SecKeyRef key) {
    CFErrorRef error = NULL;
    CFDataRef keyData = SecKeyCopyExternalRepresentation(key, &error);

    if (!keyData) {
        if (error) CFRelease(error);
        throw std::runtime_error("Failed to export key");
    }

    const uint8_t* bytes = CFDataGetBytePtr(keyData);
    std::vector<uint8_t> result(bytes, bytes + CFDataGetLength(keyData));
    CFRelease(keyData);

    return result;
}

SecKeyRef KeyManagerIOS::getSecureEnclaveKey() {
    return getOrCreateSecureEnclaveKey();
}

std::vector<uint8_t> KeyManagerIOS::wrapMasterKey(const std::vector<uint8_t>& masterKey, SecKeyRef publicKey) {
    CFDataRef plainData = CFDataCreate(kCFAllocatorDefault, masterKey.data(), masterKey.size());
    if (!plainData) {
        throw std::runtime_error("Failed to create CFData from master key");
    }

    CFErrorRef error = NULL;
    SecKeyRef encryptedKey = SecKeyCreateEncryptedData(
        publicKey,
        kSecKeyAlgorithmECIESEncryptionCofactorVariableIVX963SHA256AESGCM,
        plainData,
        &error
    );

    CFRelease(plainData);

    if (!encryptedKey) {
        if (error) CFRelease(error);
        throw std::runtime_error("Failed to wrap master key");
    }

    std::vector<uint8_t> result = secKeyToVector(encryptedKey);
    CFRelease(encryptedKey);

    return result;
}

std::vector<uint8_t> KeyManagerIOS::unwrapMasterKey(const std::vector<uint8_t>& wrappedKey, SecKeyRef privateKey) {
    CFDataRef cipherData = CFDataCreate(kCFAllocatorDefault, wrappedKey.data(), wrappedKey.size());
    if (!cipherData) {
        throw std::runtime_error("Failed to create CFData from wrapped key");
    }

    CFErrorRef error = NULL;
    SecKeyRef decryptedKey = SecKeyCreateDecryptedData(
        privateKey,
        kSecKeyAlgorithmECIESEncryptionCofactorVariableIVX963SHA256AESGCM,
        cipherData,
        &error
    );

    CFRelease(cipherData);

    if (!decryptedKey) {
        if (error) CFRelease(error);
        throw std::runtime_error("Failed to unwrap master key");
    }

    std::vector<uint8_t> result = secKeyToVector(decryptedKey);
    CFRelease(decryptedKey);

    return result;
}

std::vector<uint8_t> KeyManagerIOS::getOrGenerateMasterKey(const std::string& alias) {
    @autoreleasepool {
        NSString *nsAlias = [NSString stringWithUTF8String:alias.c_str()];
        NSData *tag = [nsAlias dataUsingEncoding:NSUTF8StringEncoding];

        NSDictionary *query = @{
            (__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService: @"SecureDB",
            (__bridge id)kSecAttrAccount: nsAlias,
            (__bridge id)kSecReturnData: @YES,
            (__bridge id)kSecUseAuthenticationUI: (__bridge id)kSecUseAuthenticationUIAllow
        };

        CFTypeRef result = NULL;
        OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);

        if (status == errSecSuccess && result) {
            NSData *wrappedKey = (__bridge NSData*)result;
            std::vector<uint8_t> wrapped(wrappedKey.bytes, wrappedKey.bytes + wrappedKey.length);

            SecKeyRef privateKey = getOrCreateSecureEnclaveKey();
            try {
                return unwrapMasterKey(wrapped, privateKey);
            } catch (...) {
                CFRelease(privateKey);
                throw;
            }
        }

        SecKeyRef privateKey = getOrCreateSecureEnclaveKey();
        SecKeyRef publicKey = SecKeyCopyPublicKey(privateKey);

        if (!publicKey) {
            CFRelease(privateKey);
            throw std::runtime_error("Failed to get public key");
        }

        std::vector<uint8_t> masterKey(32);
        CCRandomGenerateBytes(masterKey.data(), masterKey.size());

        std::vector<uint8_t> wrapped = wrapMasterKey(masterKey, publicKey);

        NSDictionary *addQuery = @{
            (__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService: @"SecureDB",
            (__bridge id)kSecAttrAccount: nsAlias,
            (__bridge id)kSecValueData: [NSData dataWithBytes:wrapped.data() length:wrapped.size()],
            (__bridge id)kSecUseAuthenticationUI: (__bridge id)kSecUseAuthenticationUIAllow
        };

        status = SecItemAdd((__bridge CFDictionaryRef)addQuery, NULL);

        CFRelease(publicKey);
        CFRelease(privateKey);

        if (status != errSecSuccess && status != errSecDuplicateItem) {
            throw std::runtime_error("Failed to store wrapped key");
        }

        return masterKey;
    }
}

bool KeyManagerIOS::deleteMasterKey(const std::string& alias) {
    @autoreleasepool {
        NSString *nsAlias = [NSString stringWithUTF8String:alias.c_str()];

        NSDictionary *query = @{
            (__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService: @"SecureDB",
            (__bridge id)kSecAttrAccount: nsAlias
        };

        OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query);
        return status == errSecSuccess || status == errSecItemNotFound;
    }
}

} // namespace secure_db