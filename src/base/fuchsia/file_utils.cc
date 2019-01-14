// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/file_utils.h"

#include <fcntl.h>

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

base::File GetFileFromHandle(zx::handle handle) {
  base::ScopedFD fd;
  zx_handle_t handles[1] = {handle.release()};
  zx_handle_t types[1] = {PA_FDIO_REMOTE};
  zx_status_t status = fdio_create_fd(handles, types, 1, fd.receive());
  if (status != ZX_OK) {
    ZX_LOG(WARNING, status) << "fdio_create_fd";
    return base::File();
  }

  // Verify that the FD is file-like by querying it with a file-specific fcntl.
  int flags = fcntl(fd.get(), F_GETFL);
  if (flags == -1) {
    LOG(WARNING) << "Handle is not a valid file descriptor.";

    // Release the FD using FDIO directly instead of the ScopedFD
    // destructor. ScopedFD calls close() which isn't supported by this FD.
    fdio_t* fdio_to_drop;
    status = fdio_unbind_from_fd(fd.release(), &fdio_to_drop);
    ZX_CHECK(status == ZX_OK, status) << "fdio_unbind_from_fd";

    return base::File();
  }

  return base::File(fd.release());
}

}  // namespace fuchsia
}  // namespace base
