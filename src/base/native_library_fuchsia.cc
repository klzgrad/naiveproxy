// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/native_library.h"

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <lib/zx/vmo.h>
#include <stdio.h>
#include <zircon/dlfcn.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "base/base_paths_fuchsia.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/fuchsia/fuchsia_logging.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/posix/safe_strerror.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"

namespace base {

std::string NativeLibraryLoadError::ToString() const {
  return message;
}

NativeLibrary LoadNativeLibraryWithOptions(const FilePath& library_path,
                                           const NativeLibraryOptions& options,
                                           NativeLibraryLoadError* error) {
  std::vector<base::FilePath::StringType> components;
  library_path.GetComponents(&components);
  if (components.size() != 1u) {
    NOTREACHED() << "library_path is a path, should be a filename: "
                 << library_path.MaybeAsASCII();
    return nullptr;
  }

  FilePath computed_path;
  base::PathService::Get(DIR_SOURCE_ROOT, &computed_path);
  computed_path = computed_path.AppendASCII("lib").Append(components[0]);
  base::File library(computed_path,
                     base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!library.IsValid()) {
    if (error) {
      error->message = base::StringPrintf(
          "open library: %s",
          base::File::ErrorToString(library.error_details()).c_str());
    }
    return nullptr;
  }

  zx::vmo vmo;
  zx_status_t status = fdio_get_vmo_clone(library.GetPlatformFile(),
                                          vmo.reset_and_get_address());
  if (status != ZX_OK) {
    if (error) {
      error->message = base::StringPrintf("fdio_get_vmo_clone: %s",
                                          zx_status_get_string(status));
    }
    return nullptr;
  }

  // VMOs must be marked as exec-capable to be mapped executable in dlopen_vmo,
  // and fdio_get_vmo_clone shouldn't be marking every VMO it returns
  // exec-capable.  So we should mark it as exec-capable here.
  // In the fullness of time, this invalid handle should be swapped out for a
  // ZX_RSRC_KIND_VMEX handle.
  status = vmo.replace_as_executable(zx::handle(), &vmo);
  if (status != ZX_OK) {
    if (error) {
      error->message = base::StringPrintf("zx_vmo_replace_as_executable: %s",
                                          zx_status_get_string(status));
    }
    return nullptr;
  }

  NativeLibrary result = dlopen_vmo(vmo.get(), RTLD_LAZY | RTLD_LOCAL);
  return result;
}

void UnloadNativeLibrary(NativeLibrary library) {
  // dlclose() is a no-op on Fuchsia, so do nothing here.
}

void* GetFunctionPointerFromNativeLibrary(NativeLibrary library,
                                          StringPiece name) {
  return dlsym(library, name.data());
}

std::string GetNativeLibraryName(StringPiece name) {
  return base::StringPrintf("lib%s.so", name.as_string().c_str());
}

std::string GetLoadableModuleName(StringPiece name) {
  return GetNativeLibraryName(name);
}

}  // namespace base
