// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_COMPLETION_CALLBACK_H_
#define NET_BASE_COMPLETION_CALLBACK_H_

#include <stdint.h>

#include "base/callback.h"
#include "base/cancelable_callback.h"

namespace net {

// A callback specialization that takes a single int parameter. Usually this is
// used to report a byte count or network error code.
typedef base::Callback<void(int)> CompletionCallback;

// 64bit version of callback specialization that takes a single int64_t
// parameter. Usually this is used to report a file offset, size or network
// error code.
typedef base::Callback<void(int64_t)> Int64CompletionCallback;

typedef base::CancelableCallback<void(int)> CancelableCompletionCallback;

}  // namespace net

#endif  // NET_BASE_COMPLETION_CALLBACK_H_
