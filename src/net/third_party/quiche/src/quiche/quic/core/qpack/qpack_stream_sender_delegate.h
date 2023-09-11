// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QPACK_QPACK_STREAM_SENDER_DELEGATE_H_
#define QUICHE_QUIC_CORE_QPACK_QPACK_STREAM_SENDER_DELEGATE_H_

#include "absl/strings/string_view.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// This interface writes encoder/decoder data to peer.
class QUIC_EXPORT_PRIVATE QpackStreamSenderDelegate {
 public:
  virtual ~QpackStreamSenderDelegate() = default;

  // Write data on the unidirectional stream.
  virtual void WriteStreamData(absl::string_view data) = 0;

  // Return the number of bytes buffered due to underlying stream being blocked.
  virtual uint64_t NumBytesBuffered() const = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QPACK_QPACK_STREAM_SENDER_DELEGATE_H_
