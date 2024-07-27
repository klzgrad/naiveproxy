// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/quic_spdy_client_stream.h"

#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/core/http/quic_spdy_client_session.h"
#include "quiche/quic/core/http/spdy_utils.h"
#include "quiche/quic/core/http/web_transport_http3.h"
#include "quiche/quic/core/quic_alarm.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/platform/api/quiche_flag_utils.h"
#include "quiche/common/quiche_text_utils.h"
#include "quiche/spdy/core/spdy_protocol.h"

using spdy::Http2HeaderBlock;

namespace quic {

QuicSpdyClientStream::QuicSpdyClientStream(QuicStreamId id,
                                           QuicSpdyClientSession* session,
                                           StreamType type)
    : QuicSpdyStream(id, session, type),
      content_length_(-1),
      response_code_(0),
      header_bytes_read_(0),
      header_bytes_written_(0),
      session_(session) {}

QuicSpdyClientStream::QuicSpdyClientStream(PendingStream* pending,
                                           QuicSpdyClientSession* session)
    : QuicSpdyStream(pending, session),
      content_length_(-1),
      response_code_(0),
      header_bytes_read_(0),
      header_bytes_written_(0),
      session_(session) {}

QuicSpdyClientStream::~QuicSpdyClientStream() = default;

bool QuicSpdyClientStream::CopyAndValidateHeaders(
    const QuicHeaderList& header_list, int64_t& content_length,
    spdy::Http2HeaderBlock& headers) {
  return SpdyUtils::CopyAndValidateHeaders(header_list, &content_length,
                                           &headers);
}

bool QuicSpdyClientStream::ParseAndValidateStatusCode() {
  if (!ParseHeaderStatusCode(response_headers_, &response_code_)) {
    QUIC_DLOG(ERROR) << "Received invalid response code: "
                     << response_headers_[":status"].as_string()
                     << " on stream " << id();
    Reset(QUIC_BAD_APPLICATION_PAYLOAD);
    return false;
  }

  if (response_code_ == 101) {
    // 101 "Switching Protocols" is forbidden in HTTP/3 as per the
    // "HTTP Upgrade" section of draft-ietf-quic-http.
    QUIC_DLOG(ERROR) << "Received forbidden 101 response code"
                     << " on stream " << id();
    Reset(QUIC_BAD_APPLICATION_PAYLOAD);
    return false;
  }

  if (response_code_ >= 100 && response_code_ < 200) {
    // These are Informational 1xx headers, not the actual response headers.
    QUIC_DLOG(INFO) << "Received informational response code: "
                    << response_headers_[":status"].as_string() << " on stream "
                    << id();
    set_headers_decompressed(false);
    preliminary_headers_.push_back(std::move(response_headers_));
  }

  return true;
}

void QuicSpdyClientStream::OnInitialHeadersComplete(
    bool fin, size_t frame_len, const QuicHeaderList& header_list) {
  QuicSpdyStream::OnInitialHeadersComplete(fin, frame_len, header_list);
  time_to_response_headers_received_ =
      session()->GetClock()->ApproximateNow() - creation_time();
  QUICHE_DCHECK(headers_decompressed());
  header_bytes_read_ += frame_len;
  if (rst_sent()) {
    // QuicSpdyStream::OnInitialHeadersComplete already rejected invalid
    // response header.
    return;
  }

  if (!CopyAndValidateHeaders(header_list, content_length_,
                              response_headers_)) {
    QUIC_DLOG(ERROR) << "Failed to parse header list: "
                     << header_list.DebugString() << " on stream " << id();
    Reset(QUIC_BAD_APPLICATION_PAYLOAD);
    return;
  }

  if (web_transport() != nullptr) {
    web_transport()->HeadersReceived(response_headers_);
    if (!web_transport()->ready()) {
      // The request was rejected by WebTransport, typically due to not having a
      // 2xx status.  The reason we're using Reset() here rather than closing
      // cleanly is to avoid having to process the response body.
      Reset(QUIC_STREAM_CANCELLED);
      return;
    }
  }

  if (!ParseAndValidateStatusCode()) {
    return;
  }

  if (uses_capsules() && (response_code_ < 200 || response_code_ >= 300)) {
    capsules_failed_ = true;
  }

  ConsumeHeaderList();
  QUIC_DVLOG(1) << "headers complete for stream " << id();
}

void QuicSpdyClientStream::OnTrailingHeadersComplete(
    bool fin, size_t frame_len, const QuicHeaderList& header_list) {
  QuicSpdyStream::OnTrailingHeadersComplete(fin, frame_len, header_list);
  MarkTrailersConsumed();
}

void QuicSpdyClientStream::OnBodyAvailable() {
  while (HasBytesToRead()) {
    struct iovec iov;
    if (GetReadableRegions(&iov, 1) == 0) {
      // No more data to read.
      break;
    }
    QUIC_DVLOG(1) << "Client processed " << iov.iov_len << " bytes for stream "
                  << id();
    data_.append(static_cast<char*>(iov.iov_base), iov.iov_len);

    if (content_length_ >= 0 &&
        data_.size() > static_cast<uint64_t>(content_length_)) {
      QUIC_DLOG(ERROR) << "Invalid content length (" << content_length_
                       << ") with data of size " << data_.size();
      Reset(QUIC_BAD_APPLICATION_PAYLOAD);
      return;
    }
    MarkConsumed(iov.iov_len);
  }
  if (sequencer()->IsClosed()) {
    OnFinRead();
  } else {
    sequencer()->SetUnblocked();
  }
}

size_t QuicSpdyClientStream::SendRequest(Http2HeaderBlock headers,
                                         absl::string_view body, bool fin) {
  QuicConnection::ScopedPacketFlusher flusher(session_->connection());
  bool send_fin_with_headers = fin && body.empty();
  size_t bytes_sent = body.size();
  header_bytes_written_ =
      WriteHeaders(std::move(headers), send_fin_with_headers, nullptr);
  bytes_sent += header_bytes_written_;

  if (!body.empty()) {
    WriteOrBufferBody(body, fin);
  }

  return bytes_sent;
}

bool QuicSpdyClientStream::ValidateReceivedHeaders(
    const QuicHeaderList& header_list) {
  if (!QuicSpdyStream::ValidateReceivedHeaders(header_list)) {
    return false;
  }
  // Verify the presence of :status header.
  bool saw_status = false;
  for (const std::pair<std::string, std::string>& pair : header_list) {
    if (pair.first == ":status") {
      saw_status = true;
    } else if (absl::StrContains(pair.first, ":")) {
      set_invalid_request_details(
          absl::StrCat("Unexpected ':' in header ", pair.first, "."));
      QUIC_DLOG(ERROR) << invalid_request_details();
      return false;
    }
  }
  if (!saw_status) {
    set_invalid_request_details("Missing :status in response header.");
    QUIC_DLOG(ERROR) << invalid_request_details();
    return false;
  }
  return saw_status;
}

void QuicSpdyClientStream::OnFinRead() {
  time_to_response_complete_ =
      session()->GetClock()->ApproximateNow() - creation_time();
  QuicSpdyStream::OnFinRead();
}

}  // namespace quic
