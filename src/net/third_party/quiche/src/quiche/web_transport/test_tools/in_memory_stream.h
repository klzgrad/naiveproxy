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
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/platform/api/quiche_test.h"
#include "quiche/common/quiche_mem_slice.h"
#include "quiche/web_transport/web_transport.h"

namespace webtransport::test {

// InMemoryStream models an incoming readable WebTransport stream where all of
// the data is read from an in-memory buffer.  Writes are unsupported by
// default, but a subclass can handle those by overriding
// OnWrite/OnFin/GetWriteStatus.
class QUICHE_NO_EXPORT InMemoryStream : public Stream {
 public:
  explicit InMemoryStream(StreamId id) : id_(id) {}

  // webtransport::Stream implementation.
  [[nodiscard]] ReadResult Read(absl::Span<char> output) override;
  [[nodiscard]] ReadResult Read(std::string* output) override;
  size_t ReadableBytes() const override;
  PeekResult PeekNextReadableRegion() const override;
  bool SkipBytes(size_t bytes) override;

  absl::Status Writev(absl::Span<quiche::QuicheMemSlice> data,
                      const StreamWriteOptions& options) override;
  bool CanWrite() const override {
    return GetWriteStatusWithExtraChecks().ok();
  }

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

  // If set to true, PeekNextReadableRegion() will return a single one-byte
  // readable region at a time.
  void set_peek_one_byte_at_a_time(bool peek_one_byte_at_a_time) {
    peek_one_byte_at_a_time_ = peek_one_byte_at_a_time;
  }

  bool fin_sent() const { return fin_sent_; }

 protected:
  virtual void OnWrite(absl::string_view data) {}
  virtual void OnFin() {}
  virtual absl::Status GetWriteStatus() const;

 private:
  void Terminate();
  absl::Status GetWriteStatusWithExtraChecks() const;

  StreamId id_;
  std::unique_ptr<StreamVisitor> visitor_;
  StreamPriority priority_;
  absl::Cord buffer_;
  bool fin_received_ = false;
  bool abruptly_terminated_ = false;
  bool peek_one_byte_at_a_time_ = false;
  bool fin_sent_ = false;
};

// An InMemoryStream where all of the write-side interactions are exposed as
// mock methods.
class QUICHE_NO_EXPORT InMemoryStreamWithMockWrite : public InMemoryStream {
 public:
  explicit InMemoryStreamWithMockWrite(StreamId id);

  MOCK_METHOD(void, OnWrite, (absl::string_view data), (override));
  MOCK_METHOD(void, OnFin, (), (override));
  MOCK_METHOD(absl::Status, GetWriteStatus, (), (const, override));
};

// An InMemoryStream where all writes are stored into a buffer.
class QUICHE_NO_EXPORT InMemoryStreamWithWriteBuffer : public InMemoryStream {
 public:
  using InMemoryStream::InMemoryStream;

  void OnWrite(absl::string_view data) { write_buffer_.append(data); }
  absl::Status GetWriteStatus() const { return absl::OkStatus(); }

  std::string& write_buffer() { return write_buffer_; }

 private:
  std::string write_buffer_;
};

}  // namespace webtransport::test

#endif  // QUICHE_WEB_TRANSPORT_TEST_TOOLS_IN_MEMORY_STREAM_H_
