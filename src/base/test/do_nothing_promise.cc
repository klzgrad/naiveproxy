// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/test/do_nothing_promise.h"

namespace base {

DoNothingPromiseBuilder::operator scoped_refptr<internal::AbstractPromise>()
    const {
  return internal::NoOpPromiseExecutor::Create(from_here, can_resolve,
                                               can_reject, reject_policy);
}

}  // namespace base
