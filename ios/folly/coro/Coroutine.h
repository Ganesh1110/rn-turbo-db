#pragma once
// ── folly/coro/Coroutine.h compatibility stub ──────────────────────────────
// ReactNativeDependencies ships folly headers but NOT folly/coro/*.
// folly/Expected.h and folly/Optional.h conditionally include this file when
// FOLLY_HAS_COROUTINES is truthy (auto-detected from C++20 <coroutine>).
//
// This shim provides minimal fallback definitions so Folly headers compile
// even when coroutines are disabled via FOLLY_HAS_COROUTINES=0.

#if defined(FOLLY_HAS_COROUTINES) && FOLLY_HAS_COROUTINES
#include <coroutine>
#endif

namespace folly {
namespace coro {

#if defined(FOLLY_HAS_COROUTINES) && FOLLY_HAS_COROUTINES
// Real C++20 coroutine support — alias standard primitives
template <typename Promise = void>
using coroutine_handle = std::coroutine_handle<Promise>;

using suspend_never  = std::suspend_never;
using suspend_always = std::suspend_always;
#else
// Fallback stubs when coroutines are disabled — satisfy Folly header #include
// These are never actually used at runtime; they just satisfy compile-time checks.
struct coroutine_handle {
  template <typename Promise>
  structpromise_type {};
};
struct suspend_never {
  constexpr bool await_ready() const noexcept { return true; }
  template <typename Promise>
  constexpr void await_suspend(std::coroutine_handle<Promise>) const noexcept {}
  constexpr void await_resume() const noexcept {}
};
struct suspend_always {
  constexpr bool await_ready() const noexcept { return false; }
  template <typename Promise>
  constexpr void await_suspend(std::coroutine_handle<Promise>) const noexcept {}
  constexpr void await_resume() const noexcept {}
};
#endif

// Fallback for missing detect_promise_return_object_eager_conversion in older Folly.
// Returns false to force deferred (non-eager) coroutine conversion path.
// This satisfies Folly Optional.h / Expected.h when real coro support isn't present.
inline bool detect_promise_return_object_eager_conversion() {
  return false;
}

} // namespace coro
} // namespace folly