// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/rand_util.h"

#include <zircon/syscalls.h>

#include <algorithm>

#include "base/logging.h"

namespace base {

void RandBytes(void* output, size_t output_length) {
  size_t remaining = output_length;
  unsigned char* cur = reinterpret_cast<unsigned char*>(output);
  while (remaining > 0) {
    // The syscall has a maximum number of bytes that can be read at once.
    size_t read_len =
        std::min(remaining, static_cast<size_t>(ZX_CPRNG_DRAW_MAX_LEN));

    size_t actual;
    zx_status_t status = zx_cprng_draw(cur, read_len, &actual);
    CHECK(status == ZX_OK && read_len == actual);

    CHECK(remaining >= actual);
    remaining -= actual;
    cur += actual;
  }
}

}  // namespace base
