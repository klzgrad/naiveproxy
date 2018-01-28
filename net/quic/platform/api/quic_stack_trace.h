// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_PLATFORM_API_QUIC_STACK_TRACE_H_
#define NET_QUIC_PLATFORM_API_QUIC_STACK_TRACE_H_

#include "net/quic/platform/impl/quic_stack_trace_impl.h"

namespace net {

inline std::string QuicStackTrace() {
  return QuicStackTraceImpl();
}

}  // namespace net

#endif  // NET_QUIC_PLATFORM_API_QUIC_STACK_TRACE_H_
