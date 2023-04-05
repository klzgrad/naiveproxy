// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_HTTP_WEB_TRANSPORT_STREAM_ADAPTER_H_
#define QUICHE_QUIC_CORE_HTTP_WEB_TRANSPORT_STREAM_ADAPTER_H_

#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_stream_sequencer.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/web_transport_interface.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

// Converts WebTransportStream API calls into QuicStream API calls.  The users
// of this class can either subclass it, or wrap around it.
class QUIC_EXPORT_PRIVATE WebTransportStreamAdapter
    : public WebTransportStream {
 public:
  WebTransportStreamAdapter(QuicSession* session, QuicStream* stream,
                            QuicStreamSequencer* sequencer);

  // WebTransportStream implementation.
  ABSL_MUST_USE_RESULT ReadResult Read(absl::Span<char> output) override;
  ABSL_MUST_USE_RESULT ReadResult Read(std::string* output) override;
  absl::Status Writev(absl::Span<const absl::string_view> data,
                      const quiche::StreamWriteOptions& options) override;
  bool CanWrite() const override;
  size_t ReadableBytes() const override;
  void SetVisitor(std::unique_ptr<WebTransportStreamVisitor> visitor) override {
    visitor_ = std::move(visitor);
  }
  QuicStreamId GetStreamId() const override { return stream_->id(); }

  void ResetWithUserCode(WebTransportStreamError error) override;
  void ResetDueToInternalError() override {
    stream_->Reset(QUIC_STREAM_INTERNAL_ERROR);
  }
  void SendStopSending(WebTransportStreamError error) override;
  void MaybeResetDueToStreamObjectGone() override {
    if (stream_->write_side_closed() && stream_->read_side_closed()) {
      return;
    }
    stream_->Reset(QUIC_STREAM_CANCELLED);
  }

  WebTransportStreamVisitor* visitor() override { return visitor_.get(); }

  // Calls that need to be passed from the corresponding QuicStream methods.
  void OnDataAvailable();
  void OnCanWriteNewData();

 private:
  absl::Status CheckBeforeStreamWrite() const;

  QuicSession* session_;            // Unowned.
  QuicStream* stream_;              // Unowned.
  QuicStreamSequencer* sequencer_;  // Unowned.
  std::unique_ptr<WebTransportStreamVisitor> visitor_;
  bool fin_read_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_WEB_TRANSPORT_STREAM_ADAPTER_H_
