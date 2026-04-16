#include "SecureDBImpl.h"
#include "DBEngine.h"

#ifdef __APPLE__
#import <Foundation/Foundation.h>
#import <Security/Security.h>
#include "SodiumCryptoContext.h"
#include "KeyManagerIOS.h"
#include "PlatformUtilsIOS.h"
#endif

namespace facebook::react {

static std::vector<uint8_t> getDeviceMasterKey() {
    try {
        return secure_db::KeyManagerIOS::getOrGenerateMasterKey("securedb-master-key");
    } catch (const std::exception& e) {
        NSLog(@"[SecureDB] Failed to get Secure Enclave key: %s", e.what());
        std::vector<uint8_t> key(32);
        for (int i = 0; i < 32; i++) {
            key[i] = static_cast<uint8_t>(0xAC ^ i);
        }
        return key;
    }
}

SecureDBImpl::SecureDBImpl(
  std::shared_ptr<CallInvoker> jsInvoker
)
  : NativeSecureDBCxxSpec(std::move(jsInvoker)) {
}

bool SecureDBImpl::install(jsi::Runtime& rt) {
    std::unique_ptr<secure_db::SodiumCryptoContext> crypto = nullptr;
#ifdef __APPLE__
    crypto = std::make_unique<secure_db::SodiumCryptoContext>();
    crypto->setMasterKey(getDeviceMasterKey());
#endif

    secure_db::installDBEngine(rt, jsInvoker_, std::move(crypto));
    return true;
}

std::string SecureDBImpl::getDocumentsDirectory(jsi::Runtime& rt) {
#ifdef __APPLE__
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *basePath = [paths firstObject];
    if (basePath == nil) {
        return "";
    }
    return [basePath UTF8String];
#else
    return "";
#endif
}

std::string SecureDBImpl::getVersion(jsi::Runtime& rt) {
    return "1.0.0";
}

}
