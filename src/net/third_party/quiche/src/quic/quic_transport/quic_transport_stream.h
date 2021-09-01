// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_STREAM_H_
#define QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_STREAM_H_

#include <cstddef>
#include <memory>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "quic/core/quic_session.h"
#include "quic/core/quic_stream.h"
#include "quic/core/quic_types.h"
#include "quic/core/web_transport_interface.h"
#include "quic/core/web_transport_stream_adapter.h"
#include "quic/quic_transport/quic_transport_session_interface.h"

namespace quic {

// QuicTransportStream is an extension of QuicStream that provides I/O interface
// that is safe to use in the QuicTransport context.  The interface ensures no
// application data is processed before the client indication is processed.
class QUIC_EXPORT_PRIVATE QuicTransportStream : public QuicStream,
                                                public WebTransportStream {
 public:
  QuicTransportStream(QuicStreamId id,
                      QuicSession* session,
                      QuicTransportSessionInterface* session_interface);

  // Reads at most |buffer_size| bytes into |buffer| and returns the number of
  // bytes actually read.
  ReadResult Read(char* buffer, size_t buffer_size) override;
  // Reads all available data and appends it to the end of |output|.
  ReadResult Read(std::string* output) override;
  // Writes |data| into the stream.  Returns true on success.
  ABSL_MUST_USE_RESULT bool Write(absl::string_view data) override;
  // Sends the FIN on the stream.  Returns true on success.
  ABSL_MUST_USE_RESULT bool SendFin() override;

  // Indicates whether it is possible to write into stream right now.
  bool CanWrite() const override;
  // Indicates the number of bytes that can be read from the stream.
  size_t ReadableBytes() const override;

  // QuicSession method implementations.
  void OnDataAvailable() override;
  void OnCanWriteNewData() override;

  QuicStreamId GetStreamId() const override { return id(); }

  void ResetWithUserCode(QuicRstStreamErrorCode error) override {
    adapter_.ResetWithUserCode(error);
  }
  void ResetDueToInternalError() override {
    adapter_.ResetDueToInternalError();
  }
  void MaybeResetDueToStreamObjectGone() override {
    adapter_.MaybeResetDueToStreamObjectGone();
  }

  WebTransportStreamVisitor* visitor() override { return adapter_.visitor(); }
  void SetVisitor(std::unique_ptr<WebTransportStreamVisitor> visitor) override {
    adapter_.SetVisitor(std::move(visitor));
  }

 protected:
  // Hide the methods that allow writing data without checking IsSessionReady().
  using QuicStream::WriteMemSlices;
  using QuicStream::WriteOrBufferData;

  void MaybeNotifyFinRead();

  WebTransportStreamAdapter adapter_;
  QuicTransportSessionInterface* session_interface_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QUIC_TRANSPORT_QUIC_TRANSPORT_STREAM_H_
