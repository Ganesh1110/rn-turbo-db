#pragma once

#include <memory>
#include <string>

#if __has_include(<TurboDBSpecJSI.h>)
#include <TurboDBSpecJSI.h>
#elif __has_include("TurboDBSpecJSI.h")
#include "TurboDBSpecJSI.h"
#endif

namespace facebook::react {

class JSI_EXPORT TurboDBImpl : public NativeTurboDBCxxSpec<TurboDBImpl> {
public:
  TurboDBImpl(std::shared_ptr<CallInvoker> jsInvoker);

  bool install(jsi::Runtime& rt);
  std::string getDocumentsDirectory(jsi::Runtime& rt);
  bool isInitialized(jsi::Runtime& rt);
  std::string getVersion(jsi::Runtime& rt);
};

} // namespace facebook::react
