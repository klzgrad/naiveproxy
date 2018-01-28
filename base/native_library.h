// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_NATIVE_LIBRARY_H_
#define BASE_NATIVE_LIBRARY_H_

// This file defines a cross-platform "NativeLibrary" type which represents
// a loadable module.

#include <string>

#include "base/base_export.h"
#include "base/strings/string_piece.h"
#include "build/build_config.h"

#if defined(OS_WIN)
#include <windows.h>
#elif defined(OS_MACOSX)
#import <CoreFoundation/CoreFoundation.h>
#endif  // OS_*

namespace base {

class FilePath;

#if defined(OS_WIN)
using NativeLibrary = HMODULE;
#elif defined(OS_MACOSX)
enum NativeLibraryType {
  BUNDLE,
  DYNAMIC_LIB
};
enum NativeLibraryObjCStatus {
  OBJC_UNKNOWN,
  OBJC_PRESENT,
  OBJC_NOT_PRESENT,
};
struct NativeLibraryStruct {
  NativeLibraryType type;
  CFBundleRefNum bundle_resource_ref;
  NativeLibraryObjCStatus objc_status;
  union {
    CFBundleRef bundle;
    void* dylib;
  };
};
using NativeLibrary = NativeLibraryStruct*;
#elif defined(OS_POSIX)
using NativeLibrary = void*;
#endif  // OS_*

struct BASE_EXPORT NativeLibraryLoadError {
#if defined(OS_WIN)
  NativeLibraryLoadError() : code(0) {}
#endif  // OS_WIN

  // Returns a string representation of the load error.
  std::string ToString() const;

#if defined(OS_WIN)
  DWORD code;
#else
  std::string message;
#endif  // OS_WIN
};

struct BASE_EXPORT NativeLibraryOptions {
  NativeLibraryOptions() = default;
  NativeLibraryOptions(const NativeLibraryOptions& options) = default;

  // If |true|, a loaded library is required to prefer local symbol resolution
  // before considering global symbols. Note that this is already the default
  // behavior on most systems. Setting this to |false| does not guarantee the
  // inverse, i.e., it does not force a preference for global symbols over local
  // ones.
  bool prefer_own_symbols = false;
};

// Loads a native library from disk.  Release it with UnloadNativeLibrary when
// you're done.  Returns NULL on failure.
// If |error| is not NULL, it may be filled in on load error.
BASE_EXPORT NativeLibrary LoadNativeLibrary(const FilePath& library_path,
                                            NativeLibraryLoadError* error);

// Loads a native library from disk.  Release it with UnloadNativeLibrary when
// you're done.  Returns NULL on failure.
// If |error| is not NULL, it may be filled in on load error.
BASE_EXPORT NativeLibrary LoadNativeLibraryWithOptions(
    const FilePath& library_path,
    const NativeLibraryOptions& options,
    NativeLibraryLoadError* error);

// Unloads a native library.
BASE_EXPORT void UnloadNativeLibrary(NativeLibrary library);

// Gets a function pointer from a native library.
BASE_EXPORT void* GetFunctionPointerFromNativeLibrary(NativeLibrary library,
                                                      StringPiece name);

// Returns the full platform specific name for a native library.
// |name| must be ASCII.
// For example:
// "mylib" returns "mylib.dll" on Windows, "libmylib.so" on Linux,
// "libmylib.dylib" on Mac.
BASE_EXPORT std::string GetNativeLibraryName(StringPiece name);

}  // namespace base

#endif  // BASE_NATIVE_LIBRARY_H_
