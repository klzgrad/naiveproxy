// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FUCHSIA_FILE_UTILS_H_
#define BASE_FUCHSIA_FILE_UTILS_H_

#include <lib/zx/handle.h>

#include "base/base_export.h"

namespace base {

class File;

namespace fuchsia {

// Gets a Zircon handle from a file or directory |path| in the process'
// namespace.
BASE_EXPORT zx::handle GetHandleFromFile(base::File file);

// Makes a File object from a Zircon handle.
// Returns an empty File if |handle| is invalid or not a valid PA_FDIO_REMOTE
// descriptor.
BASE_EXPORT base::File GetFileFromHandle(zx::handle handle);

}  // namespace fuchsia
}  // namespace base

#endif  // BASE_FUCHSIA_FILE_UTILS_H_
