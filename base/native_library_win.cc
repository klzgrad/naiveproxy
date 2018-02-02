// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/native_library.h"

#include <windows.h>

#include "base/files/file_util.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"

namespace base {

using AddDllDirectory = HMODULE (*)(PCWSTR new_directory);

namespace {
// This enum is used to back an UMA histogram, and should therefore be treated
// as append-only.
enum LoadLibraryResult {
  // LoadLibraryExW API/flags are available and the call succeeds.
  SUCCEED = 0,
  // LoadLibraryExW API/flags are availabe to use but the call fails, then
  // LoadLibraryW is used and succeeds.
  FAIL_AND_SUCCEED,
  // LoadLibraryExW API/flags are availabe to use but the call fails, then
  // LoadLibraryW is used but fails as well.
  FAIL_AND_FAIL,
  // LoadLibraryExW API/flags are unavailabe to use, then LoadLibraryW is used
  // and succeeds.
  UNAVAILABLE_AND_SUCCEED,
  // LoadLibraryExW API/flags are unavailabe to use, then LoadLibraryW is used
  // but fails.
  UNAVAILABLE_AND_FAIL,
  // Add new items before this one, always keep this one at the end.
  END
};

// A helper method to log library loading result to UMA.
void LogLibrarayLoadResultToUMA(LoadLibraryResult result) {
  UMA_HISTOGRAM_ENUMERATION("LibraryLoader.LoadNativeLibraryWindows", result,
                            LoadLibraryResult::END);
}

// A helper method to check if AddDllDirectory method is available, thus
// LOAD_LIBRARY_SEARCH_* flags are available on systems.
bool AreSearchFlagsAvailable() {
  // The LOAD_LIBRARY_SEARCH_* flags are available on systems that have
  // KB2533623 installed. To determine whether the flags are available, use
  // GetProcAddress to get the address of the AddDllDirectory,
  // RemoveDllDirectory, or SetDefaultDllDirectories function. If GetProcAddress
  // succeeds, the LOAD_LIBRARY_SEARCH_* flags can be used with LoadLibraryEx.
  // https://msdn.microsoft.com/en-us/library/windows/desktop/ms684179(v=vs.85).aspx
  // The LOAD_LIBRARY_SEARCH_* flags are used in the LoadNativeLibraryHelper
  // method.
  auto add_dll_dir_func = reinterpret_cast<AddDllDirectory>(
      GetProcAddress(GetModuleHandle(L"kernel32.dll"), "AddDllDirectory"));
  return !!add_dll_dir_func;
}

// A helper method to encode the library loading result to enum
// LoadLibraryResult.
LoadLibraryResult GetLoadLibraryResult(bool are_search_flags_available,
                                       bool has_load_library_succeeded) {
  LoadLibraryResult result;
  if (are_search_flags_available) {
    if (has_load_library_succeeded)
      result = LoadLibraryResult::FAIL_AND_SUCCEED;
    else
      result = LoadLibraryResult::FAIL_AND_FAIL;
  } else if (has_load_library_succeeded) {
    result = LoadLibraryResult::UNAVAILABLE_AND_SUCCEED;
  } else {
    result = LoadLibraryResult::UNAVAILABLE_AND_FAIL;
  }
  return result;
}

NativeLibrary LoadNativeLibraryHelper(const FilePath& library_path,
                                      NativeLibraryLoadError* error) {
  // LoadLibrary() opens the file off disk.
  AssertBlockingAllowed();

  HMODULE module = nullptr;

  // This variable records the library loading result.
  LoadLibraryResult load_library_result = LoadLibraryResult::SUCCEED;

  bool are_search_flags_available = AreSearchFlagsAvailable();
  if (are_search_flags_available) {
    // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR flag is needed to search the library
    // directory as the library may have dependencies on DLLs in this
    // directory.
    module = ::LoadLibraryExW(
        library_path.value().c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    // If LoadLibraryExW succeeds, log this metric and return.
    if (module) {
      LogLibrarayLoadResultToUMA(load_library_result);
      return module;
    }
    // GetLastError() needs to be called immediately after
    // LoadLibraryExW call.
    if (error)
      error->code = GetLastError();
  }

  // If LoadLibraryExW API/flags are unavailable or API call fails, try
  // LoadLibraryW API.
  // TODO(chengx): Currently, if LoadLibraryExW API call fails, LoadLibraryW is
  // still tried. We should strictly prefer the LoadLibraryExW over the
  // LoadLibraryW if LoadLibraryW is statistically showing no extra benefits. If
  // UMA metric shows that FAIL_AND_FAIL is the primary failure mode and/or
  // FAIL_AND_SUCCESS is close to zero, we should remove this fallback.
  // (http://crbug.com/701944)

  // Switch the current directory to the library directory as the library
  // may have dependencies on DLLs in this directory.
  bool restore_directory = false;
  FilePath current_directory;
  if (GetCurrentDirectory(&current_directory)) {
    FilePath plugin_path = library_path.DirName();
    if (!plugin_path.empty()) {
      SetCurrentDirectory(plugin_path);
      restore_directory = true;
    }
  }

  module = ::LoadLibraryW(library_path.value().c_str());

  // GetLastError() needs to be called immediately after LoadLibraryW call.
  if (!module && error)
    error->code = GetLastError();

  if (restore_directory)
    SetCurrentDirectory(current_directory);

  // Get the library loading result and log it to UMA.
  LogLibrarayLoadResultToUMA(
      GetLoadLibraryResult(are_search_flags_available, !!module));

  return module;
}
}  // namespace

std::string NativeLibraryLoadError::ToString() const {
  return StringPrintf("%lu", code);
}

// static
NativeLibrary LoadNativeLibraryWithOptions(const FilePath& library_path,
                                           const NativeLibraryOptions& options,
                                           NativeLibraryLoadError* error) {
  return LoadNativeLibraryHelper(library_path, error);
}

// static
void UnloadNativeLibrary(NativeLibrary library) {
  FreeLibrary(library);
}

// static
void* GetFunctionPointerFromNativeLibrary(NativeLibrary library,
                                          StringPiece name) {
  return GetProcAddress(library, name.data());
}

// static
std::string GetNativeLibraryName(StringPiece name) {
  DCHECK(IsStringASCII(name));
  return name.as_string() + ".dll";
}

}  // namespace base
