// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/native_library.h"

#include "base/logging.h"

#include "base/strings/string_util.h"

namespace base {

std::string NativeLibraryLoadError::ToString() const {
  return message;
}

// static
NativeLibrary LoadNativeLibraryWithOptions(const base::FilePath& library_path,
                                           const NativeLibraryOptions& options,
                                           NativeLibraryLoadError* error) {
  NOTIMPLEMENTED();
  if (error)
    error->message = "Not implemented.";
  return nullptr;
}

// static
void UnloadNativeLibrary(NativeLibrary library) {
  NOTIMPLEMENTED();
  DCHECK(!library);
}

// static
void* GetFunctionPointerFromNativeLibrary(NativeLibrary library,
                                          StringPiece name) {
  NOTIMPLEMENTED();
  return nullptr;
}

// static
std::string GetNativeLibraryName(StringPiece name) {
  DCHECK(IsStringASCII(name));
  return name.as_string();
}

}  // namespace base
