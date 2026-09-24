// Copyright (c) 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/tools/quic_simple_client_stream.h"

#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

void QuicSimpleClientStream::OnBodyAvailable() {
  if (!drop_response_body_) {
    QuicSpdyClientStream::OnBodyAvailable();
    return;
  }

  while (HasBytesToRead()) {
    struct iovec iov;
    if (GetReadableRegions(&iov, 1) == 0) {
      break;
    }
    MarkConsumed(iov.iov_len);
  }
  if (sequencer()->IsClosed()) {
    OnFinRead();
  } else {
    sequencer()->SetUnblocked();
  }
}

bool QuicSimpleClientStream::ParseAndValidateStatusCode() {
  const size_t num_previous_interim_headers = preliminary_headers().size();
  if (!QuicSpdyClientStream::ParseAndValidateStatusCode()) {
    return false;
  }
  // The base ParseAndValidateStatusCode() may have added a preliminary header.
  if (preliminary_headers().size() > num_previous_interim_headers) {
    QUICHE_DCHECK_EQ(preliminary_headers().size(),
                     num_previous_interim_headers + 1);
    if (on_interim_headers_) {
      on_interim_headers_(preliminary_headers().back());
    }
  }
  return true;
}

}  // namespace quic
