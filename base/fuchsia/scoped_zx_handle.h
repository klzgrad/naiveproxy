// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FUCHSIA_SCOPED_ZX_HANDLE_H_
#define BASE_FUCHSIA_SCOPED_ZX_HANDLE_H_

#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "base/logging.h"
#include "base/scoped_generic.h"

namespace base {

namespace internal {

struct ScopedZxHandleTraits {
  static zx_handle_t InvalidValue() { return ZX_HANDLE_INVALID; }
  static void Free(zx_handle_t object) {
    zx_status_t status = zx_handle_close(object);
    CHECK_EQ(ZX_OK, status) << zx_status_get_string(status);
  }
};

}  // namespace internal

using ScopedZxHandle =
    ScopedGeneric<zx_handle_t, internal::ScopedZxHandleTraits>;

}  // namespace base

#endif  // BASE_FUCHSIA_SCOPED_ZX_HANDLE_H_
