// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/spdy/core/spdy_no_op_visitor.h"

#include <type_traits>

namespace spdy {
namespace test {

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

}  // namespace test
}  // namespace spdy
