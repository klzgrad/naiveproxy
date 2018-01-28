// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_DEBUG_CRASH_LOGGING_H_
#define BASE_DEBUG_CRASH_LOGGING_H_

#include <stddef.h>

#include <string>
#include <type_traits>
#include <vector>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/strings/string_piece.h"

// These functions add metadata to the upload payload when sending crash reports
// to the crash server.
//
// IMPORTANT: On OS X and Linux, the key/value pairs are only sent as part of
// the upload and are not included in the minidump!

namespace base {
namespace debug {

class StackTrace;

// Sets or clears a specific key-value pair from the crash metadata. Keys and
// values are terminated at the null byte.
BASE_EXPORT void SetCrashKeyValue(const base::StringPiece& key,
                                  const base::StringPiece& value);
BASE_EXPORT void ClearCrashKey(const base::StringPiece& key);

// Records the given StackTrace into a crash key.
BASE_EXPORT void SetCrashKeyToStackTrace(const base::StringPiece& key,
                                         const StackTrace& trace);

// Formats |count| instruction pointers from |addresses| using %p and
// sets the resulting string as a value for crash key |key|. A maximum of 23
// items will be encoded, since breakpad limits values to 255 bytes.
BASE_EXPORT void SetCrashKeyFromAddresses(const base::StringPiece& key,
                                          const void* const* addresses,
                                          size_t count);

// A scoper that sets the specified key to value for the lifetime of the
// object, and clears it on destruction.
class BASE_EXPORT ScopedCrashKey {
 public:
  ScopedCrashKey(const base::StringPiece& key, const base::StringPiece& value);
  ~ScopedCrashKey();

  // Helper to force a static_assert when instantiating a ScopedCrashKey
  // temporary without a name. The usual idiom is to just #define a macro that
  // static_asserts with the message; however, that doesn't work well when the
  // type is in a namespace.
  //
  // Instead, we use a templated helper to trigger the static_assert, observing
  //   two rules:
  // - The static_assert needs to be in a normally uninstantiated template;
  //   otherwise, it will fail to compile =)
  // - Similarly, the static_assert must be dependent on the template argument,
  //   to prevent it from being evaluated until the template is instantiated.
  //
  // To prevent this constructor from being accidentally invoked, it takes a
  // special enum as an argument.

  // Finally, note that this can't just be a template function that takes only
  // one parameter, because this ends up triggering the vexing parse issue.
  enum ScopedCrashKeyNeedsNameTag {
    KEY_NEEDS_NAME,
  };

  template <typename... Args>
  explicit ScopedCrashKey(ScopedCrashKeyNeedsNameTag, const Args&...) {
    constexpr bool always_false = sizeof...(Args) == 0 && sizeof...(Args) != 0;
    static_assert(
        always_false,
        "scoped crash key objects should not be unnamed temporaries.");
  }

 private:
  std::string key_;

  DISALLOW_COPY_AND_ASSIGN(ScopedCrashKey);
};

// Disallow an instantation of ScopedCrashKey without a name, since this results
// in a temporary that is immediately destroyed. Doing so will trigger the
// static_assert in the templated constructor helper in ScopedCrashKey.
#define ScopedCrashKey(...) \
  ScopedCrashKey(base::debug::ScopedCrashKey::KEY_NEEDS_NAME, __VA_ARGS__)

// Before setting values for a key, all the keys must be registered.
struct BASE_EXPORT CrashKey {
  // The name of the crash key, used in the above functions.
  const char* key_name;

  // The maximum length for a value. If the value is longer than this, it will
  // be truncated. If the value is larger than the |chunk_max_length| passed to
  // InitCrashKeys() but less than this value, it will be split into multiple
  // numbered chunks.
  size_t max_length;
};

// Before the crash key logging mechanism can be used, all crash keys must be
// registered with this function. The function returns the amount of space
// the crash reporting implementation should allocate space for the registered
// crash keys. |chunk_max_length| is the maximum size that a value in a single
// chunk can be.
BASE_EXPORT size_t InitCrashKeys(const CrashKey* const keys, size_t count,
                                 size_t chunk_max_length);

// Returns the corresponding crash key object or NULL for a given key.
BASE_EXPORT const CrashKey* LookupCrashKey(const base::StringPiece& key);

// In the platform crash reporting implementation, these functions set and
// clear the NUL-terminated key-value pairs.
typedef void (*SetCrashKeyValueFuncT)(const base::StringPiece&,
                                      const base::StringPiece&);
typedef void (*ClearCrashKeyValueFuncT)(const base::StringPiece&);

// Sets the function pointers that are used to integrate with the platform-
// specific crash reporting libraries.
BASE_EXPORT void SetCrashKeyReportingFunctions(
    SetCrashKeyValueFuncT set_key_func,
    ClearCrashKeyValueFuncT clear_key_func);

// Helper function that breaks up a value according to the parameters
// specified by the crash key object.
BASE_EXPORT std::vector<std::string> ChunkCrashKeyValue(
    const CrashKey& crash_key,
    const base::StringPiece& value,
    size_t chunk_max_length);

// Resets the crash key system so it can be reinitialized. For testing only.
BASE_EXPORT void ResetCrashLoggingForTesting();

}  // namespace debug
}  // namespace base

#endif  // BASE_DEBUG_CRASH_LOGGING_H_
