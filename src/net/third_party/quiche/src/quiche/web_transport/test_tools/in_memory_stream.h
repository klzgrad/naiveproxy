// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_WEB_TRANSPORT_TEST_TOOLS_IN_MEMORY_STREAM_H_
#define QUICHE_WEB_TRANSPORT_TEST_TOOLS_IN_MEMORY_STREAM_H_

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_stream.h"
#include "quiche/web_transport/web_transport.h"

namespace webtransport::test {

// InMemoryStream models an incoming readable WebTransport stream where all of
// the data is read from an in-memory buffer.
class QUICHE_NO_EXPORT InMemoryStream : public Stream {
 public:
  explicit InMemoryStream(StreamId id) : id_(id) {}

  // quiche::ReadStream implementation.
  [[nodiscard]] ReadResult Read(absl::Span<char> output) override;
  [[nodiscard]] ReadResult Read(std::string* output) override;
  size_t ReadableBytes() const override;
  PeekResult PeekNextReadableRegion() const override;
  bool SkipBytes(size_t bytes) override;

  // quiche::WriteStream implementation.
  absl::Status Writev(absl::Span<const absl::string_view> data,
                      const quiche::StreamWriteOptions& options) override {
    QUICHE_NOTREACHED() << "Writev called on a read-only stream";
    return absl::UnimplementedError("Writev called on a read-only stream");
  }
  bool CanWrite() const override { return false; }

  void AbruptlyTerminate(absl::Status) override { Terminate(); }

  // webtransport::Stream implementation.
  StreamId GetStreamId() const override { return id_; }
  void ResetWithUserCode(StreamErrorCode) override {
    QUICHE_NOTREACHED() << "Reset called on a read-only stream";
  }
  void ResetDueToInternalError() override {
    QUICHE_NOTREACHED() << "Reset called on a read-only stream";
  }
  void MaybeResetDueToStreamObjectGone() override {
    QUICHE_NOTREACHED() << "Reset called on a read-only stream";
  }
  void SendStopSending(StreamErrorCode) override { Terminate(); }

  const StreamPriority& priority() const { return priority_; }
  void SetPriority(const StreamPriority& priority) override {
    priority_ = priority;
  }
  StreamVisitor* visitor() override { return visitor_.get(); }
  void SetVisitor(std::unique_ptr<StreamVisitor> visitor) override {
    visitor_ = std::move(visitor);
  }

  // Simulates receiving the specified stream data by appending it to the buffer
  // and executing the visitor callback.
  void Receive(absl::string_view data, bool fin = false);

 private:
  void Terminate();

  StreamId id_;
  std::unique_ptr<StreamVisitor> visitor_;
  StreamPriority priority_;
  absl::Cord buffer_;
  bool fin_received_ = false;
  bool abruptly_terminated_ = false;
};

}  // namespace webtransport::test

#endif  // QUICHE_WEB_TRANSPORT_TEST_TOOLS_IN_MEMORY_STREAM_H_
