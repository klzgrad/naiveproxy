// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/scoped_zx_handle.h"

#include <lib/zx/channel.h>

namespace base {

// static
ScopedZxHandle ScopedZxHandle::FromZxChannel(zx::channel channel) {
  return ScopedZxHandle(channel.release());
}

}  // namespace base
