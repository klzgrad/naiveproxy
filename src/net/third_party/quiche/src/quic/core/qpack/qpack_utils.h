// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_UTILS_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_UTILS_H_

#include "net/third_party/quiche/src/quic/core/qpack/qpack_stream_sender_delegate.h"

namespace quic {
// TODO(renjietang): Move this class to qpack_test_utils.h once it is not needed
// in QuicSpdySession.
class QUIC_EXPORT_PRIVATE NoopQpackStreamSenderDelegate
    : public QpackStreamSenderDelegate {
 public:
  ~NoopQpackStreamSenderDelegate() override = default;

  void WriteStreamData(QuicStringPiece /*data*/) override {}
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_UTILS_H_
