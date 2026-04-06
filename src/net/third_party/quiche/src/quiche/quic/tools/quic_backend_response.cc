// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_backend_response.h"

namespace quic {

QuicBackendResponse::QuicBackendResponse()
    : response_type_(REGULAR_RESPONSE), delay_(QuicTime::Delta::Zero()) {}

QuicBackendResponse::~QuicBackendResponse() = default;

}  // namespace quic
