// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_STREAM_RECEIVER_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_STREAM_RECEIVER_H_

#include "absl/strings/string_view.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// This interface decodes QPACK data that are received on a QpackReceiveStream.
class QUICHE_EXPORT QpackStreamReceiver {
 public:
  virtual ~QpackStreamReceiver() = default;

  // Decode data.
  virtual void Decode(absl::string_view data) = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_STREAM_RECEIVER_H_
