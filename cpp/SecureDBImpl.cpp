#include "SecureDBImpl.h"
#include "DBEngine.h"

#ifdef __APPLE__
#include "../ios/SecureCryptoIOS.h"
#endif

namespace facebook::react {

SecureDBImpl::SecureDBImpl(
  std::shared_ptr<CallInvoker> jsInvoker
)
  : NativeSecureDBCxxSpec(std::move(jsInvoker)) {
}

void SecureDBImpl::install(jsi::Runtime& rt) {
    std::unique_ptr<secure_db::SecureCryptoContext> crypto = nullptr;
#ifdef __APPLE__
    crypto = std::make_unique<secure_db::SecureCryptoContext>(std::make_unique<secure_db::SecureCryptoIOS>());
#else
    // For Android, we'll need to pass the JavaVM to the constructor
    // This is typically handled in the JNI layer
#endif

    secure_db::installDBEngine(rt, std::move(crypto));
}

std::string SecureDBImpl::getVersion(jsi::Runtime& rt) {
    return "1.0.0";
}

}