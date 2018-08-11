// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/default_job.h"

#include <zircon/process.h>

#include "base/logging.h"

namespace base {

namespace {
zx_handle_t g_job = ZX_HANDLE_INVALID;
}  // namespace

zx_handle_t GetDefaultJob() {
  if (g_job == ZX_HANDLE_INVALID)
    return zx_job_default();
  return g_job;
}

void SetDefaultJob(ScopedZxHandle job) {
  DCHECK_EQ(ZX_HANDLE_INVALID, g_job);
  g_job = job.release();
}

}  // namespace base
