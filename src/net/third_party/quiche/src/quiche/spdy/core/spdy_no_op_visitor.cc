// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/spdy/core/spdy_no_op_visitor.h"

#include <cstdint>
#include <type_traits>

#include "quiche/spdy/core/spdy_headers_handler_interface.h"
#include "quiche/spdy/core/spdy_protocol.h"

namespace spdy {

SpdyNoOpVisitor::SpdyNoOpVisitor() {
  static_assert(std::is_abstract<SpdyNoOpVisitor>::value == false,
                "Need to update SpdyNoOpVisitor.");
}
SpdyNoOpVisitor::~SpdyNoOpVisitor() = default;

SpdyHeadersHandlerInterface* SpdyNoOpVisitor::OnHeaderFrameStart(
    SpdyStreamId /*stream_id*/) {
  return this;
}

bool SpdyNoOpVisitor::OnUnknownFrame(SpdyStreamId /*stream_id*/,
                                     uint8_t /*frame_type*/) {
  return true;
}

}  // namespace spdy
