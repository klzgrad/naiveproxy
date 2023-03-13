// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/web_transport_stream_adapter.h"

#include "absl/status/status.h"
#include "quiche/quic/core/http/web_transport_http3.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/quiche_buffer_allocator.h"
#include "quiche/common/quiche_mem_slice_storage.h"
#include "quiche/web_transport/web_transport.h"

namespace quic {

WebTransportStreamAdapter::WebTransportStreamAdapter(
    QuicSession* session, QuicStream* stream, QuicStreamSequencer* sequencer)
    : session_(session), stream_(stream), sequencer_(sequencer) {}

WebTransportStream::ReadResult WebTransportStreamAdapter::Read(
    absl::Span<char> buffer) {
  iovec iov;
  iov.iov_base = buffer.data();
  iov.iov_len = buffer.size();
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
  ReadResult result =
      Read(absl::Span<char>(&(*output)[old_size], bytes_to_read));
  QUICHE_DCHECK_EQ(bytes_to_read, result.bytes_read);
  output->resize(old_size + result.bytes_read);
  return result;
}

absl::Status WebTransportStreamAdapter::Writev(
    absl::Span<const absl::string_view> data,
    const quiche::StreamWriteOptions& options) {
  if (data.empty() && !options.send_fin()) {
    return absl::InvalidArgumentError(
        "Writev() called without any data or a FIN");
  }
  const absl::Status initial_check_status = CheckBeforeStreamWrite();
  if (!initial_check_status.ok()) {
    return initial_check_status;
  }

  std::vector<iovec> iovecs;
  size_t total_size = 0;
  iovecs.resize(data.size());
  for (size_t i = 0; i < data.size(); i++) {
    // QuicheMemSliceStorage only reads iovec, thus this is safe.
    iovecs[i].iov_base = const_cast<char*>(data[i].data());
    iovecs[i].iov_len = data[i].size();
    total_size += data[i].size();
  }
  quiche::QuicheMemSliceStorage storage(
      iovecs.data(), iovecs.size(),
      session_->connection()->helper()->GetStreamSendBufferAllocator(),
      GetQuicFlag(quic_send_buffer_max_data_slice_size));
  QuicConsumedData consumed =
      stream_->WriteMemSlices(storage.ToSpan(), /*fin=*/options.send_fin());

  if (consumed.bytes_consumed == total_size) {
    return absl::OkStatus();
  }
  if (consumed.bytes_consumed == 0) {
    return absl::UnavailableError("Stream write-blocked");
  }
  // WebTransportStream::Write() is an all-or-nothing write API.  To achieve
  // that property, it relies on WriteMemSlices() being an all-or-nothing API.
  // If WriteMemSlices() fails to provide that guarantee, we have no way to
  // communicate a partial write to the caller, and thus it's safer to just
  // close the connection.
  constexpr absl::string_view kErrorMessage =
      "WriteMemSlices() unexpectedly partially consumed the input data";
  QUIC_BUG(WebTransportStreamAdapter partial write)
      << kErrorMessage << ", provided: " << total_size
      << ", written: " << consumed.bytes_consumed;
  stream_->OnUnrecoverableError(QUIC_INTERNAL_ERROR,
                                std::string(kErrorMessage));
  return absl::InternalError(kErrorMessage);
}

absl::Status WebTransportStreamAdapter::CheckBeforeStreamWrite() const {
  if (stream_->write_side_closed() || stream_->fin_buffered()) {
    return absl::FailedPreconditionError("Stream write side is closed");
  }
  if (!stream_->CanWriteNewData()) {
    return absl::UnavailableError("Stream write-blocked");
  }
  return absl::OkStatus();
}

bool WebTransportStreamAdapter::CanWrite() const {
  return CheckBeforeStreamWrite().ok();
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
