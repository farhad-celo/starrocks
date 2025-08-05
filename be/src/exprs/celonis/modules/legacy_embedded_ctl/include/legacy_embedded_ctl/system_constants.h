#pragma once

namespace celonis::accelerator::legacy_embedded_ctl {

static constexpr char PATH_SEPARATOR{'/'};

// Compiler
#if defined(__clang__)
#define CEL_COMPILER_CLANG
#elif defined(__GNUC__)
#define CEL_COMPILER_GNU
#else
static_assert(false, "Unknown compiler is not supported");
#endif

// C++ Standard library implementation
// -stdlib=libstdc++
#if defined(__GLIBCXX__)
#define CEL_STD_LIB_GNU

// -stdlib=libc++
#elif defined(_LIBCPP_VERSION)
#define CEL_STD_LIB_LLVM

#else
static_assert(false, "Unknown C++ standard library implementation is not supported");
#endif

#ifdef NDEBUG
constexpr bool IS_DEBUG_BUILD{false};
#else
constexpr bool IS_DEBUG_BUILD{true};
#endif

#ifdef CEL_COMPILER_CLANG
constexpr bool IS_ASAN_BUILD{__has_feature(address_sanitizer)};
#endif

#ifdef CEL_COMPILER_GNU
#if defined(__SANITIZE_ADDRESS__)
constexpr bool IS_ASAN_BUILD{true};
#else
constexpr bool IS_ASAN_BUILD{false};
#endif
#endif

}  // namespace celonis::accelerator::legacy_embedded_ctl
