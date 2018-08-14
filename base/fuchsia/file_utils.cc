// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/file_utils.h"

#include <lib/fdio/limits.h>
#include <lib/fdio/util.h>
#include <zircon/processargs.h>
#include <utility>

#include "base/files/file.h"
#include "base/fuchsia/fuchsia_logging.h"

namespace base {
namespace fuchsia {

zx::handle GetHandleFromFile(File file) {
  // Unwrap the FD into |handles|. Negative result indicates failure.
  zx_handle_t handles[FDIO_MAX_HANDLES] = {};
  uint32_t types[FDIO_MAX_HANDLES] = {};
  zx_status_t num_handles =
      fdio_transfer_fd(file.TakePlatformFile(), 0, handles, types);
  if (num_handles <= 0) {
    DCHECK_LT(num_handles, 0);
    ZX_DLOG(ERROR, num_handles) << "fdio_transfer_fd";
    return zx::handle();
  }

  // Wrap the returned handles, so they will be closed on error.
  zx::handle owned_handles[FDIO_MAX_HANDLES];
  for (int i = 0; i < FDIO_MAX_HANDLES; ++i)
    owned_handles[i] = zx::handle(handles[i]);

  // We expect a single handle, of type PA_FDIO_REMOTE.
  if (num_handles != 1 || types[0] != PA_FDIO_REMOTE) {
    DLOG(ERROR) << "Specified file has " << num_handles
                << " handles, and type: " << types[0];
    return zx::handle();
  }

  return std::move(owned_handles[0]);
}

}  // namespace fuchsia
}  // namespace base
