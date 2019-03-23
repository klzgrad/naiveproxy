// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_LOG_NET_LOG_PARAMETERS_CALLBACK_H_
#define NET_LOG_NET_LOG_PARAMETERS_CALLBACK_H_

#include <memory>

#include "base/callback_forward.h"
#include "net/log/net_log_capture_mode.h"

namespace base {
class Value;
}

namespace net {

// A callback that returns a Value representation of the parameters
// associated with an event.  If called, it will be called synchronously,
// so it need not have owning references.  May be called more than once, or
// not at all.  May return nullptr.
typedef base::Callback<std::unique_ptr<base::Value>(NetLogCaptureMode)>
    NetLogParametersCallback;

}  // namespace net

#endif  // NET_LOG_NET_LOG_PARAMETERS_CALLBACK_H_
