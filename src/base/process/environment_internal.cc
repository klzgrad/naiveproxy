// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/environment_internal.h"

#include <stddef.h>

#include <algorithm>
#include <vector>

#include "base/check.h"
#include "base/compiler_specific.h"
#include "base/containers/span.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)
#include <string.h>

#if BUILDFLAG(IS_APPLE)
#include <crt_externs.h>
#else
extern char** environ;
#endif
#endif

#if BUILDFLAG(IS_WIN)
#include <windows.h>

#include "base/check_op.h"
#endif

namespace base::internal {

namespace {

// Parses a null-terminated input string of an environment block. The key is
// placed into the given string, and the total length of the line, including
// the terminating null, is returned.
size_t ParseEnvLine(NativeEnvironmentCStringView input,
                    NativeEnvironmentStringView* key) {
  // Skip to the equals or end of the string, this is the key.
  size_t loc = input.find('=');
  *key = input.substr(0, loc);
  // +1 to account for the null terminator.
  return input.size() + 1;
}

}  // namespace

#if BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)

// Returns a span over the current process' environment. Each string in the
// span is null-terminated.
base::span<const char* const> GetEnvironment() {
#if BUILDFLAG(IS_APPLE)
  char** environ = *_NSGetEnviron();
#endif
  size_t count = 0;
  if (environ) {
    // SAFETY: environ is a null-terminated array of pointers. See
    // https://man7.org/linux/man-pages/man7/environ.7.html for details.
    for (char** env = environ; *env; UNSAFE_BUFFERS(++env)) {
      count++;
    }
  }
  // SAFETY: We have just calculated the size of the environ array.
  return UNSAFE_BUFFERS(base::span<const char* const>(environ, count));
}

void SetEnvironment(base::span<char*> env) {
  CHECK(env.empty() || env.back() == nullptr);
#if BUILDFLAG(IS_APPLE)
  *_NSGetEnviron() = env.empty() ? nullptr : env.data();
#else
  environ = env.empty() ? nullptr : env.data();
#endif
}

// PRECONDITIONS: Each string in 'env' must be null-terminated. The caller is
// responsible for ensuring this.
UNSAFE_BUFFER_USAGE base::HeapArray<char*> AlterEnvironment(
    const base::span<const char* const> env,
    const EnvironmentMap& changes) {
  std::string value_storage;  // Holds concatenated null-terminated strings.
  std::vector<size_t> result_indices;  // Line indices into value_storage.

  // First build up all of the unchanged environment strings. These are
  // null-terminated of the form "key=value".
  NativeEnvironmentStringView key;
  for (const char* const env_var : env) {
    // SAFETY: Each string in `env` is guaranteed to be null-terminated by the
    // precondition of AlterEnvironment().
    size_t line_length = ParseEnvLine(
        UNSAFE_BUFFERS(NativeEnvironmentCStringView(env_var)), &key);

    // Keep only values not specified in the change vector.
    auto found_change = changes.find(key);
    if (found_change == changes.end()) {
      result_indices.push_back(value_storage.size());
      value_storage.append(env_var, line_length);
    }
  }

  // Now append all modified and new values.
  for (const auto& i : changes) {
    if (!i.second.empty()) {
      result_indices.push_back(value_storage.size());
      value_storage.append(i.first);
      value_storage.push_back('=');
      value_storage.append(i.second);
      value_storage.push_back(0);
    }
  }

  size_t pointer_count_required =
      result_indices.size() + 1 +  // Null-terminated array of pointers.
      (value_storage.size() + sizeof(char*) - 1) / sizeof(char*);  // Buffer.
  auto result = base::HeapArray<char*>::WithSize(pointer_count_required);

  if (!value_storage.empty()) {
    // The string storage goes after the array of pointers.
    // Here `as_writable_chars` converts char* found in the HeapArray to char
    // so they can copy the value_storage in
    base::span<char> storage_data =
        base::as_writable_chars(result.subspan(result_indices.size() + 1));
    storage_data.copy_prefix_from(value_storage);

    // Fill array of pointers at the beginning of the result.
    for (size_t i = 0; i < result_indices.size(); i++) {
      result[i] = &storage_data[result_indices[i]];
    }
  }
  result[result_indices.size()] = 0;  // Null terminator.

  return result;
}

#elif BUILDFLAG(IS_WIN)

// static
base::HeapArray<wchar_t> GetEnvironment() {
  wchar_t* env_ptr = GetEnvironmentStrings();
  if (!env_ptr) {
    return {};
  }

  const wchar_t* p = env_ptr;
  while (*p) {
    // SAFETY: env_ptr points to a memory block provided by the OS. The loop
    // follows the Win32 API contract where strings are NUL-terminated and the
    // entire block ends with an extra NUL.
    const size_t length = UNSAFE_BUFFERS(wcslen(p));
    p = UNSAFE_BUFFERS(p + length + 1);
  }
  // Include the final terminating NUL.
  const size_t total_len = static_cast<size_t>(p - env_ptr) + 1;
  auto result = base::HeapArray<wchar_t>::WithSize(total_len);

  // SAFETY: env_ptr points to a block of memory provided by the OS, and
  // total_len has been calculated by scanning that same block until the
  // double-NUL terminator.
  base::span<const wchar_t> env_span =
      UNSAFE_BUFFERS(base::span(env_ptr, total_len));
  result.copy_from(env_span);

  FreeEnvironmentStrings(env_ptr);
  return result;
}

NativeEnvironmentString AlterEnvironment(base::span<const wchar_t> env,
                                         const EnvironmentMap& changes) {
  NativeEnvironmentString result;

  // First build up all of the unchanged environment strings.
  base::span<const wchar_t> remaining = env;
  while (!remaining.empty() && remaining[0] != L'\0') {
    // Find the next NUL terminator.
    auto it = std::ranges::find(remaining, L'\0');
    CHECK(it != remaining.end());  // Malformed block.
    // SAFETY: We verified `remaining` contains L'\0' at `it`.
    const wchar_t* env_var_ptr = remaining.data();
    NativeEnvironmentCStringView env_var =
        UNSAFE_BUFFERS(NativeEnvironmentCStringView(env_var_ptr));
    NativeEnvironmentStringView key;
    size_t line_length = ParseEnvLine(env_var, &key);

    // Keep only values not specified in the change vector.
    if (changes.find(key) == changes.end()) {
      result.append(remaining.data(), line_length);
    }
    remaining = remaining.subspan(line_length);
  }

  // Now append all modified and new values.
  for (const auto& i : changes) {
    // Windows environment blocks cannot handle keys or values with NULs.
    CHECK_EQ(std::wstring::npos, i.first.find(L'\0'));
    CHECK_EQ(std::wstring::npos, i.second.find(L'\0'));
    if (!i.second.empty()) {
      result += i.first;
      result.push_back(L'=');
      result += i.second;
      result.push_back(L'\0');
    }
  }

  // Add the terminating NUL.
  result.push_back(L'\0');
  return result;
}

#endif  // BUILDFLAG(IS_POSIX) || BUILDFLAG(IS_FUCHSIA)

}  // namespace base::internal
