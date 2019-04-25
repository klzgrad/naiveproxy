// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/file_utils.h"

#include <lib/fdio/fd.h>

#include "base/files/file.h"
#include "base/fuchsia/fuchsia_logging.h"

namespace base {
namespace fuchsia {

zx::handle GetHandleFromFile(File file) {
  zx::handle handle;
  zx_status_t status =
      fdio_fd_transfer(file.GetPlatformFile(), handle.reset_and_get_address());
  if (status != ZX_ERR_UNAVAILABLE)
    ignore_result(file.TakePlatformFile());
  if (status == ZX_OK)
    return handle;
  ZX_DLOG(ERROR, status) << "fdio_fd_transfer";
  return zx::handle();
}

base::File GetFileFromHandle(zx::handle handle) {
  base::ScopedFD fd;
  zx_status_t status =
      fdio_fd_create(handle.release(), base::ScopedFD::Receiver(fd).get());
  if (status == ZX_OK)
    return base::File(fd.release());
  ZX_LOG(WARNING, status) << "fdio_fd_create";
  return base::File();
}

}  // namespace fuchsia
}  // namespace base
