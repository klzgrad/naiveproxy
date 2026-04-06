// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_WEB_TRANSPORT_TEST_TOOLS_H_
#define QUICHE_QUIC_TEST_TOOLS_WEB_TRANSPORT_TEST_TOOLS_H_

#include "quiche/web_transport/test_tools/mock_web_transport.h"

namespace quic::test {

using MockWebTransportSessionVisitor = ::webtransport::test::MockSessionVisitor;
using MockWebTransportStreamVisitor = ::webtransport::test::MockStreamVisitor;

}  // namespace quic::test

#endif  // QUICHE_QUIC_TEST_TOOLS_WEB_TRANSPORT_TEST_TOOLS_H_
