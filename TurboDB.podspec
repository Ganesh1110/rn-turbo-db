require "json"

package = JSON.parse(File.read(File.join(__dir__, "package.json")))

# Developer greeting — shown during pod install
puts "\e[36m"
puts "▛▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▀▜"
puts "▌  ████████ ██    ██ ██████  ██████   ██████  ▐"
puts "▌     ██    ██    ██ ██   ██ ██   ██ ██    ██ ▐"
puts "▌     ██    ██    ██ ██████  ██████  ██    ██ ▐"
puts "▌     ██    ██    ██ ██   ██ ██   ██ ██    ██ ▐"
puts "▌     ██     ██████  ██   ██ ██████   ██████  ▐"
puts "▌                                             ▐"
puts "▌   ██████  ██████                            ▐"
puts "▌   ██   ██ ██   ██                           ▐"
puts "▌   ██   ██ ██████                            ▐"
puts "▌   ██   ██ ██   ██                           ▐"
puts "▌   ██████  ██████                            ▐"
puts "▙▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▟"
puts ""
puts "⚡ Turbo DB — React Native's Fastest KV Store"
puts "\e[0m"

Pod::Spec.new do |s|
  s.name         = "TurboDB"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = package["homepage"]
  s.license      = package["license"]
  s.authors      = package["author"]

  s.platforms    = { :ios => min_ios_version_supported }
  s.source       = { :git => ".git", :tag => "#{s.version}" }

  # Source files: native ObjC/C++ bridge + shared C++ engine
  # ios/folly/coro/Coroutine.h is a compatibility stub — it satisfies the
  # #include inside folly/Expected.h when FOLLY_HAS_COROUTINES auto-detects to 1
  # but ReactNativeDependencies does not ship the real folly/coro/ directory.
  s.source_files = "ios/**/*.{h,m,mm}", "cpp/**/*.{hpp,cpp,c,h}"

  # Expose the folly compat stub as a public header while keeping all other
  # ios/ headers private. header_mappings_dir preserves the folly/coro/
  # subdirectory so the stub is installed at:
  #   $(PODS_ROOT)/Headers/Public/TurboDB/folly/coro/Coroutine.h
  s.public_header_files  = "ios/folly/**/*.h"
  s.private_header_files = "ios/**/*.h"
  s.header_mappings_dir  = "ios"

  # Exclude generated codegen files and dead ThreadPool (replaced by DBScheduler)
  s.exclude_files = "ios/generated/**/*", "cpp/ThreadPool.cpp"

  # No libsodium dependency - using stub impl until VFS issue resolved
  # s.dependency 'libsodium'

  # ── Pod-target build settings (apply to TurboDB's own compiled files) ───────
  # FOLLY_HAS_COROUTINES=0  → stops folly/Expected.h including the missing coro header
  # std=c++20               → required by DBScheduler + WALManager
  # $(inherited)            → preserves any flags the RN build system sets on the target
  s.pod_target_xcconfig = {
    "OTHER_CPLUSPLUSFLAGS" => "$(inherited) -DFOLLY_NO_CONFIG -DFOLLY_MOBILE=1 -DFOLLY_USE_LIBCPP=1 -DFOLLY_HAS_COROUTINES=0 -std=c++20",
    "CLANG_CXX_LANGUAGE_STANDARD" => "c++20",
    "HEADER_SEARCH_PATHS" => "$(inherited) \"$(PODS_TARGET_SRCROOT)/cpp\""
  }

  # ── User-target build settings (apply to the consuming app target) ──────────
  # Propagates FOLLY_HAS_COROUTINES=0 to app-level compilation units that
  # transitively pull in folly headers (e.g. via JSI/RCTBridgeless bridging).
  # Also adds TurboDB's public header dir to the search path so that the stub
  # folly/coro/Coroutine.h is found as <folly/coro/Coroutine.h> without any
  # manual Podfile configuration.
  s.user_target_xcconfig = {
    "OTHER_CPLUSPLUSFLAGS" => "$(inherited) -DFOLLY_HAS_COROUTINES=0",
    "HEADER_SEARCH_PATHS"  => "$(inherited) \"$(PODS_ROOT)/Headers/Public/TurboDB\""
  }

  install_modules_dependencies(s)
end