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

}  // namespace fuchsia
}  // namespace base

#endif  // BASE_FUCHSIA_FILE_UTILS_H_
