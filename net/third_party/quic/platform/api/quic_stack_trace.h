// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_STACK_TRACE_H_
#define NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_STACK_TRACE_H_

#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/platform/impl/quic_stack_trace_impl.h"

namespace quic {

inline QuicString QuicStackTrace() {
  return QuicStackTraceImpl();
}

}  // namespace quic

#endif  // NET_THIRD_PARTY_QUIC_PLATFORM_API_QUIC_STACK_TRACE_H_
