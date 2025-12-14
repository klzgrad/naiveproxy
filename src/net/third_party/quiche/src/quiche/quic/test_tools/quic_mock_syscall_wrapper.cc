// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_mock_syscall_wrapper.h"

using testing::_;
using testing::Invoke;

namespace quic {
namespace test {

MockQuicSyscallWrapper::MockQuicSyscallWrapper(QuicSyscallWrapper* delegate) {
  ON_CALL(*this, Sendmsg(_, _, _))
      .WillByDefault(Invoke(delegate, &QuicSyscallWrapper::Sendmsg));

  ON_CALL(*this, Sendmmsg(_, _, _, _))
      .WillByDefault(Invoke(delegate, &QuicSyscallWrapper::Sendmmsg));
}

}  // namespace test
}  // namespace quic
