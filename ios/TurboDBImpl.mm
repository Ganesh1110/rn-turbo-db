#include "TurboDBImpl.h"
#include "DBEngine.h"
#include "SodiumCryptoContext.h"

#if __has_include(<TurboDBSpecJSI.h>)
#include <TurboDBSpecJSI.h>
#elif __has_include("TurboDBSpecJSI.h")
#include "TurboDBSpecJSI.h"
#endif

#ifdef __APPLE__
#import <Foundation/Foundation.h>
#include "KeyManagerIOS.h"
#include "PlatformUtilsIOS.h"
#endif

#ifdef DEBUG
#define TURBODB_LOG(fmt, ...) NSLog(@"[TurboDB] " fmt, ##__VA_ARGS__)
#else
#define TURBODB_LOG(fmt, ...)
#endif

namespace facebook::react {

TurboDBImpl::TurboDBImpl(std::shared_ptr<CallInvoker> jsInvoker)
    : NativeTurboDBCxxSpec<TurboDBImpl>(std::move(jsInvoker)) {}

bool TurboDBImpl::install(jsi::Runtime& rt) {
    TURBODB_LOG("install: starting, runtime=%p", &rt);
    
    if (&rt == nullptr) {
        TURBODB_LOG("install: ERROR - runtime is null!");
        throw jsi::JSError(rt, "[TurboDB] JSI runtime is null - React Native JSI not initialized");
        return false;
    }
    
    auto js_invoker = this->jsInvoker_;
    
#ifdef __APPLE__
    try {
        TURBODB_LOG("install: getting documents directory");
        std::string docsDir = getDocumentsDirectory(rt);
        TURBODB_LOG("install: docsDir=%s", docsDir.c_str());
        
        TURBODB_LOG("install: getting/generating master key");
        auto masterKey = turbo_db::KeyManagerIOS::getOrGenerateMasterKey("TurboDBMasterKey");
        
        TURBODB_LOG("install: creating SodiumCryptoContext");
        auto crypto = std::make_unique<turbo_db::SodiumCryptoContext>();
        crypto->setMasterKey(masterKey);
        
        TURBODB_LOG("install: calling installDBEngine");
        turbo_db::installDBEngine(rt, js_invoker, std::move(crypto));
        
        // Verify installation
        if (rt.global().hasProperty(rt, "NativeDB")) {
            TURBODB_LOG("install: SUCCESS - NativeDB is now available on global");
            return true;
        } else {
            TURBODB_LOG("install: WARNING - NativeDB not found on global after install");
            return true;
        }
    } catch (NSException *exception) {
        TURBODB_LOG("install: NSException - %@", exception.reason);
        throw jsi::JSError(rt, [NSString stringWithFormat:@"[TurboDB] install failed: %@", exception.reason]);
    } catch (const std::exception& e) {
        TURBODB_LOG("install: std::exception - %s", e.what());
        throw jsi::JSError(rt, [NSString stringWithFormat:@"[TurboDB] install failed: %s", e.what()]);
    }
#else
    TURBODB_LOG("install: not Apple platform - skipping JSI install");
#endif
    return true;
}

std::string TurboDBImpl::getDocumentsDirectory(jsi::Runtime& rt) {
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

bool TurboDBImpl::isInitialized(jsi::Runtime& rt) {
    return rt.global().hasProperty(rt, "NativeDB");
}

std::string TurboDBImpl::getVersion(jsi::Runtime& rt) {
    return "0.1.0";
}

}
