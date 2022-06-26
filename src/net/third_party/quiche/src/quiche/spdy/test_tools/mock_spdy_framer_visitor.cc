// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/spdy/test_tools/mock_spdy_framer_visitor.h"

namespace spdy {

namespace test {

MockSpdyFramerVisitor::MockSpdyFramerVisitor() { DelegateHeaderHandling(); }

MockSpdyFramerVisitor::~MockSpdyFramerVisitor() = default;

}  // namespace test

}  // namespace spdy
