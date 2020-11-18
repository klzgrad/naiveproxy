// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_IMPL_QUIC_MACROS_IMPL_H_
#define NET_QUIC_PLATFORM_IMPL_QUIC_MACROS_IMPL_H_

#include "base/compiler_specific.h"

#define QUIC_MUST_USE_RESULT_IMPL WARN_UNUSED_RESULT
#define QUIC_UNUSED_IMPL ALLOW_UNUSED_TYPE

// TODO(wub): Use ABSL_CONST_INIT when absl is allowed.
#define QUIC_CONST_INIT_IMPL

#endif  // NET_QUIC_PLATFORM_IMPL_QUIC_MACROS_IMPL_H_
