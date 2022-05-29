// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/web_transport_stream_adapter.h"

#include "quiche/quic/core/http/web_transport_http3.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"

namespace quic {

WebTransportStreamAdapter::WebTransportStreamAdapter(
    QuicSession* session, QuicStream* stream, QuicStreamSequencer* sequencer)
    : session_(session), stream_(stream), sequencer_(sequencer) {}

WebTransportStream::ReadResult WebTransportStreamAdapter::Read(
    char* buffer, size_t buffer_size) {
  iovec iov;
  iov.iov_base = buffer;
  iov.iov_len = buffer_size;
  const size_t result = sequencer_->Readv(&iov, 1);
  if (!fin_read_ && sequencer_->IsClosed()) {
    fin_read_ = true;
    stream_->OnFinRead();
  }
  return ReadResult{result, sequencer_->IsClosed()};
}

WebTransportStream::ReadResult WebTransportStreamAdapter::Read(
    std::string* output) {
  const size_t old_size = output->size();
  const size_t bytes_to_read = ReadableBytes();
  output->resize(old_size + bytes_to_read);
  ReadResult result = Read(&(*output)[old_size], bytes_to_read);
  QUICHE_DCHECK_EQ(bytes_to_read, result.bytes_read);
  output->resize(old_size + result.bytes_read);
  return result;
}

bool WebTransportStreamAdapter::Write(absl::string_view data) {
  if (!CanWrite()) {
    return false;
  }

  quiche::QuicheMemSlice memslice(quiche::QuicheBuffer::Copy(
      session_->connection()->helper()->GetStreamSendBufferAllocator(), data));
  QuicConsumedData consumed =
      stream_->WriteMemSlices(absl::MakeSpan(&memslice, 1), /*fin=*/false);

  if (consumed.bytes_consumed == data.size()) {
    return true;
  }
  if (consumed.bytes_consumed == 0) {
    return false;
  }
  // WebTransportStream::Write() is an all-or-nothing write API.  To achieve
  // that property, it relies on WriteMemSlices() being an all-or-nothing API.
  // If WriteMemSlices() fails to provide that guarantee, we have no way to
  // communicate a partial write to the caller, and thus it's safer to just
  // close the connection.
  QUIC_BUG(WebTransportStreamAdapter partial write)
      << "WriteMemSlices() unexpectedly partially consumed the input "
         "data, provided: "
      << data.size() << ", written: " << consumed.bytes_consumed;
  stream_->OnUnrecoverableError(
      QUIC_INTERNAL_ERROR,
      "WriteMemSlices() unexpectedly partially consumed the input data");
  return false;
}

bool WebTransportStreamAdapter::SendFin() {
  if (!CanWrite()) {
    return false;
  }

  quiche::QuicheMemSlice empty;
  QuicConsumedData consumed =
      stream_->WriteMemSlices(absl::MakeSpan(&empty, 1), /*fin=*/true);
  QUICHE_DCHECK_EQ(consumed.bytes_consumed, 0u);
  return consumed.fin_consumed;
}

bool WebTransportStreamAdapter::CanWrite() const {
  return stream_->CanWriteNewData() && !stream_->write_side_closed();
}

size_t WebTransportStreamAdapter::ReadableBytes() const {
  return sequencer_->ReadableBytes();
}

void WebTransportStreamAdapter::OnDataAvailable() {
  if (visitor_ == nullptr) {
    return;
  }
  const bool fin_readable = sequencer_->IsClosed() && !fin_read_;
  if (ReadableBytes() == 0 && !fin_readable) {
    return;
  }
  visitor_->OnCanRead();
}

void WebTransportStreamAdapter::OnCanWriteNewData() {
  // Ensure the origin check has been completed, as the stream can be notified
  // about being writable before that.
  if (!CanWrite()) {
    return;
  }
  if (visitor_ != nullptr) {
    visitor_->OnCanWrite();
  }
}

void WebTransportStreamAdapter::ResetWithUserCode(
    WebTransportStreamError error) {
  stream_->ResetWriteSide(QuicResetStreamError(
      QUIC_STREAM_CANCELLED, WebTransportErrorToHttp3(error)));
}

void WebTransportStreamAdapter::SendStopSending(WebTransportStreamError error) {
  stream_->SendStopSending(QuicResetStreamError(
      QUIC_STREAM_CANCELLED, WebTransportErrorToHttp3(error)));
}

}  // namespace quic
