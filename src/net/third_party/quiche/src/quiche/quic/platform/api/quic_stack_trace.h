// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_STACK_TRACE_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_STACK_TRACE_H_

#include <string>

#include "quiche/common/platform/api/quiche_stack_trace.h"

namespace quic {

inline std::string QuicStackTrace() { return quiche::QuicheStackTrace(); }

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_STACK_TRACE_H_
