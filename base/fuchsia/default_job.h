// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FUCHSIA_DEFAULT_JOB_H_
#define BASE_FUCHSIA_DEFAULT_JOB_H_

#include "base/base_export.h"
#include "base/fuchsia/scoped_zx_handle.h"

namespace base {

// Gets and sets the job object used for creating new child processes,
// and looking them up by their process IDs.
// zx_job_default() will be returned if no job is explicitly set here.
// Only valid handles may be passed to SetDefaultJob().
BASE_EXPORT zx_handle_t GetDefaultJob();
BASE_EXPORT void SetDefaultJob(ScopedZxHandle job);

}  // namespace base

#endif  // BASE_FUCHSIA_DEFAULT_JOB_H_
