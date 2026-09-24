// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper functions for webtransport::Stream API.

#ifndef QUICHE_COMMON_QUICHE_STREAM_H_
#define QUICHE_COMMON_QUICHE_STREAM_H_

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/quiche_callbacks.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/web_transport/web_transport.h"

namespace webtransport {

// Calls `callback` for every contiguous chunk available inside the stream.
// Returns true if the FIN has been reached.
inline bool ProcessAllReadableRegions(
    Stream& stream,
    quiche::UnretainedCallback<void(absl::string_view)> callback) {
  for (;;) {
    Stream::PeekResult peek_result = stream.PeekNextReadableRegion();
    if (!peek_result.has_data()) {
      return false;
    }
    callback(peek_result.peeked_data);
    bool fin = stream.SkipBytes(peek_result.peeked_data.size());
    if (fin) {
      return true;
    }
  }
}

// Convenience methods to write a single chunk of data into the stream.
inline absl::Status WriteIntoStream(
    Stream& stream, quiche::QuicheMemSlice slice,
    const StreamWriteOptions& options = kDefaultStreamWriteOptions) {
  return stream.Writev(absl::MakeSpan(&slice, 1), options);
}
inline absl::Status WriteIntoStream(
    Stream& stream, absl::string_view data,
    const StreamWriteOptions& options = kDefaultStreamWriteOptions) {
  quiche::QuicheMemSlice slice = quiche::QuicheMemSlice::Copy(data);
  return stream.Writev(absl::MakeSpan(&slice, 1), options);
}

// Convenience methods to send a FIN on the stream.
inline absl::Status SendFinOnStream(Stream& stream) {
  StreamWriteOptions options;
  options.set_send_fin(true);
  return stream.Writev(absl::Span<quiche::QuicheMemSlice>(), options);
}

}  // namespace webtransport

#endif  // QUICHE_COMMON_QUICHE_STREAM_H_
