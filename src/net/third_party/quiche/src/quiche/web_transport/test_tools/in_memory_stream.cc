// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/web_transport/test_tools/in_memory_stream.h"

#include <cstddef>
#include <string>
#include <vector>

#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_stream.h"
#include "quiche/common/vectorized_io_utils.h"

namespace webtransport::test {

quiche::ReadStream::ReadResult InMemoryStream::Read(absl::Span<char> output) {
  std::vector<absl::string_view> chunks;
  for (absl::string_view chunk : buffer_.Chunks()) {
    chunks.push_back(chunk);
  }
  size_t bytes_read = quiche::GatherStringViewSpan(chunks, output);
  buffer_.RemovePrefix(bytes_read);
  return ReadResult{bytes_read, buffer_.empty() && fin_received_};
}

quiche::ReadStream::ReadResult InMemoryStream::Read(std::string* output) {
  ReadResult result;
  result.bytes_read = buffer_.size();
  result.fin = fin_received_;
  absl::AppendCordToString(buffer_, output);
  buffer_.Clear();
  return result;
}

size_t InMemoryStream::ReadableBytes() const { return buffer_.size(); }

quiche::ReadStream::PeekResult InMemoryStream::PeekNextReadableRegion() const {
  if (buffer_.empty()) {
    return PeekResult{"", fin_received_, fin_received_};
  }
  absl::string_view next_chunk = *buffer_.Chunks().begin();
  return PeekResult{next_chunk, false, fin_received_};
}

bool InMemoryStream::SkipBytes(size_t bytes) {
  buffer_.RemovePrefix(bytes);
  return buffer_.empty() && fin_received_;
}

void InMemoryStream::Receive(absl::string_view data, bool fin) {
  QUICHE_DCHECK(!abruptly_terminated_);
  buffer_.Append(data);
  fin_received_ |= fin;
  if (visitor_ != nullptr) {
    visitor_->OnCanRead();
  }
}

void InMemoryStream::Terminate() {
  abruptly_terminated_ = true;
  buffer_.Clear();
  fin_received_ = false;
}

}  // namespace webtransport::test
