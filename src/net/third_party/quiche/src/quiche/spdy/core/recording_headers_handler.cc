// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/spdy/core/recording_headers_handler.h"

#include <cstddef>

#include "absl/strings/string_view.h"
#include "quiche/spdy/core/spdy_headers_handler_interface.h"

namespace spdy {

RecordingHeadersHandler::RecordingHeadersHandler(
    SpdyHeadersHandlerInterface* wrapped)
    : wrapped_(wrapped) {}

void RecordingHeadersHandler::OnHeaderBlockStart() {
  block_.clear();
  if (wrapped_ != nullptr) {
    wrapped_->OnHeaderBlockStart();
  }
}

void RecordingHeadersHandler::OnHeader(absl::string_view key,
                                       absl::string_view value) {
  block_.AppendValueOrAddHeader(key, value);
  if (wrapped_ != nullptr) {
    wrapped_->OnHeader(key, value);
  }
}

void RecordingHeadersHandler::OnHeaderBlockEnd(size_t uncompressed_header_bytes,
                                               size_t compressed_header_bytes) {
  uncompressed_header_bytes_ = uncompressed_header_bytes;
  compressed_header_bytes_ = compressed_header_bytes;
  if (wrapped_ != nullptr) {
    wrapped_->OnHeaderBlockEnd(uncompressed_header_bytes,
                               compressed_header_bytes);
  }
}

}  // namespace spdy
