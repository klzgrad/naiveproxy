// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_h2_connection.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/http2_util.h"
#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/http2/adapter/oghttp2_adapter.h"
#include "openssl/base.h"
#include "openssl/err.h"
#include "openssl/ssl.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_text_utils.h"

#define ENDPOINT (is_server_ ? "Server: " : "Client: ")

using http2::adapter::Header;
using http2::adapter::Http2KnownSettingsId;

namespace quic {

namespace {

int AppendErrorToString(const char* msg, size_t msg_len, void* ctx) {
  std::string* result = reinterpret_cast<std::string*>(ctx);
  absl::StrAppend(result, "\n", absl::string_view(msg, msg_len));
  return 1;
}

absl::Status SslErrorStatus(const char* msg, int ssl_err, int ret) {
  return absl::FailedPreconditionError(FormatSslError(msg, ssl_err, ret));
}

}  // namespace

std::string FormatSslError(const char* msg, int ssl_err, int ret) {
  std::string result = absl::StrCat(msg, ": ");
  switch (ssl_err) {
    case SSL_ERROR_SSL:
      absl::StrAppend(&result, ERR_reason_error_string(ERR_peek_error()));
      break;
    case SSL_ERROR_SYSCALL:
      if (ret == 0) {
        absl::StrAppend(&result, "peer closed connection");
      } else {
        absl::StrAppend(&result, strerror(errno));
      }
      break;
    case SSL_ERROR_ZERO_RETURN:
      absl::StrAppend(&result, "received close_notify");
      break;
    default:
      absl::StrAppend(&result,
                      "unexpected error: ", SSL_error_description(ssl_err));
      break;
  }
  ERR_print_errors_cb(AppendErrorToString, &result);
  return result;
}

MasqueH2Connection::MasqueH2Connection(SSL* ssl, bool is_server,
                                       Visitor* visitor)
    : ssl_(ssl), is_server_(is_server), visitor_(visitor) {}

void MasqueH2Connection::OnTransportReadable() {
  while (TryRead()) {
  }
}

MasqueH2Connection::~MasqueH2Connection() {}

void MasqueH2Connection::Abort(absl::Status error) {
  QUICHE_CHECK(!error.ok());
  if (aborted()) {
    QUICHE_LOG(ERROR) << ENDPOINT
                      << "Connection already aborted, ignoring new error: "
                      << error.message();
    return;
  }
  error_ = error;
  QUICHE_LOG(ERROR) << ENDPOINT << "Aborting connection: " << error_.message();
  visitor_->OnConnectionFinished(this, error_);
}

void MasqueH2Connection::StartH2() {
  QUICHE_LOG(INFO) << ENDPOINT << "Starting H2";
  http2::adapter::OgHttp2Adapter::Options options;
  std::vector<Http2Setting> settings;
  if (is_server_) {
    options.perspective = http2::adapter::Perspective::kServer;
    settings.push_back(
        Http2Setting{Http2KnownSettingsId::ENABLE_CONNECT_PROTOCOL, 1});
  } else {
    options.perspective = http2::adapter::Perspective::kClient;
  }
  settings.push_back(
      Http2Setting{Http2KnownSettingsId::HEADER_TABLE_SIZE, 4096});
  settings.push_back(Http2Setting{Http2KnownSettingsId::ENABLE_PUSH, 0});
  settings.push_back(
      Http2Setting{Http2KnownSettingsId::MAX_CONCURRENT_STREAMS, 100});
  settings.push_back(
      Http2Setting{Http2KnownSettingsId::INITIAL_WINDOW_SIZE, 268435456});
  settings.push_back(Http2Setting{Http2KnownSettingsId::MAX_FRAME_SIZE, 16384});
  settings.push_back(
      Http2Setting{Http2KnownSettingsId::MAX_HEADER_LIST_SIZE, 65535});
  h2_adapter_ = http2::adapter::OgHttp2Adapter::Create(*this, options);
  h2_adapter_->SubmitSettings(settings);
  // Increase connection-level flow control window to 256MB.
  h2_adapter_->SubmitWindowUpdate(0, 268435456);
  visitor_->OnConnectionReady(this);
}
bool MasqueH2Connection::TryRead() {
  if (!tls_connected_) {
    int ssl_handshake_ret = SSL_do_handshake(ssl_);
    if (ssl_handshake_ret == 1) {
      tls_connected_ = true;
      StartH2();
      AttemptToSend();
    } else {
      int ssl_err = SSL_get_error(ssl_, ssl_handshake_ret);
      if (ssl_err == SSL_ERROR_WANT_READ) {
        QUICHE_DVLOG(1) << ENDPOINT
                        << "SSL_do_handshake will require another read";
        return false;
      }
      Abort(SslErrorStatus("Error while connecting to TLS", ssl_err,
                           ssl_handshake_ret));
      return false;
    }
  }
  uint8_t buffer[kBioBufferSize] = {};
  int ssl_read_ret = SSL_read(ssl_, buffer, sizeof(buffer) - 1);
  if (ssl_read_ret < 0) {
    int ssl_err = SSL_get_error(ssl_, ssl_read_ret);
    if (ssl_err == SSL_ERROR_WANT_READ) {
      return false;
    }
    Abort(
        SslErrorStatus("Error while reading from TLS", ssl_err, ssl_read_ret));
    return false;
  }
  if (ssl_read_ret == 0) {
    Abort(absl::AbortedError("TLS read closed"));
    return false;
  }
  QUICHE_DVLOG(1) << ENDPOINT << "Read " << ssl_read_ret << " bytes from TLS";
  QUICHE_DVLOG(2) << ENDPOINT << "Read TLS bytes:" << std::endl
                  << quiche::QuicheTextUtils::HexDump(absl::string_view(
                         reinterpret_cast<const char*>(buffer), ssl_read_ret));
  h2_adapter_->ProcessBytes(
      absl::string_view(reinterpret_cast<const char*>(buffer), ssl_read_ret));
  return AttemptToSend();
}

bool MasqueH2Connection::WriteDataToTls(absl::string_view data) {
  QUICHE_DVLOG(2) << ENDPOINT << "Writing " << data.size()
                  << " app bytes to TLS:" << std::endl
                  << quiche::QuicheTextUtils::HexDump(data);
  const bool buffered = !tls_write_buffer_.empty();
  const char* buffer_to_write;
  size_t size_to_write;
  if (buffered) {
    absl::StrAppend(&tls_write_buffer_, data);
    buffer_to_write = tls_write_buffer_.data();
    size_to_write = tls_write_buffer_.size();
  } else {
    buffer_to_write = data.data();
    size_to_write = data.size();
  }
  const int ssl_write_ret = SSL_write(ssl_, buffer_to_write, size_to_write);
  if (ssl_write_ret <= 0) {
    int ssl_err = SSL_get_error(ssl_, ssl_write_ret);
    if (ssl_err == SSL_ERROR_WANT_WRITE) {
      if (!buffered) {
        tls_write_buffer_ = std::string(data);
      }
      QUICHE_DVLOG(1) << ENDPOINT << "SSL_write will require another write, "
                      << "buffered " << tls_write_buffer_.size() << " bytes";
      return true;
    }
    Abort(SslErrorStatus("Error while writing data to TLS", ssl_err,
                         ssl_write_ret));
    return false;
  }
  if (ssl_write_ret == static_cast<int>(size_to_write)) {
    QUICHE_DVLOG(1) << ENDPOINT << "Wrote " << size_to_write << " bytes to TLS";
    if (buffered) {
      tls_write_buffer_.clear();
    }
  } else {
    QUICHE_DVLOG(1) << ENDPOINT << "Wrote " << ssl_write_ret << " / "
                    << size_to_write << " bytes to TLS and buffered the rest";
    if (buffered) {
      tls_write_buffer_.erase(0, ssl_write_ret);
    } else {
      tls_write_buffer_ = std::string(data.substr(ssl_write_ret));
    }
  }
  return true;
}

int64_t MasqueH2Connection::OnReadyToSend(absl::string_view serialized) {
  QUICHE_DVLOG(1) << ENDPOINT << "Writing " << serialized.size()
                  << " bytes of h2 data to TLS";
  if (!WriteDataToTls(serialized)) {
    return kSendError;
  }
  return serialized.size();
}

MasqueH2Connection::DataFrameHeaderInfo
MasqueH2Connection::OnReadyToSendDataForStream(Http2StreamId stream_id,
                                               size_t max_length) {
  MasqueH2Stream* stream = GetOrCreateH2Stream(stream_id);
  DataFrameHeaderInfo info;
  if (max_length < stream->body_to_send.size()) {
    info.payload_length = max_length;
    info.end_data = false;
  } else {
    info.payload_length = stream->body_to_send.size();
    info.end_data = true;
  }
  info.end_stream = info.end_data;
  return info;
}

bool MasqueH2Connection::SendDataFrame(Http2StreamId stream_id,
                                       absl::string_view frame_header,
                                       size_t payload_bytes) {
  if (!WriteDataToTls(frame_header)) {
    return false;
  }
  MasqueH2Stream* stream = GetOrCreateH2Stream(stream_id);
  size_t length_to_write = std::min(payload_bytes, stream->body_to_send.size());
  if (!WriteDataToTls(stream->body_to_send.substr(0, length_to_write))) {
    return false;
  }
  if (length_to_write == stream->body_to_send.size()) {
    stream->body_to_send.clear();
  } else {
    // Remove the written bytes from the start of `body_to_send`.
    stream->body_to_send = stream->body_to_send.substr(length_to_write);
  }
  return true;
}

void MasqueH2Connection::OnConnectionError(ConnectionError error) {
  Abort(absl::AbortedError(absl::StrCat(
      "OnConnectionError: ", http2::adapter::ConnectionErrorToString(error))));
}

void MasqueH2Connection::OnSettingsStart() {}

void MasqueH2Connection::OnSetting(Http2Setting setting) {
  QUICHE_LOG(INFO) << ENDPOINT << "Received "
                   << http2::adapter::Http2SettingsIdToString(setting.id)
                   << " = " << setting.value;
}

void MasqueH2Connection::OnSettingsEnd() {}
void MasqueH2Connection::OnSettingsAck() {}

bool MasqueH2Connection::OnBeginHeadersForStream(Http2StreamId stream_id) {
  QUICHE_DVLOG(1) << ENDPOINT << "OnBeginHeadersForStream " << stream_id;
  return true;
}

MasqueH2Connection::OnHeaderResult MasqueH2Connection::OnHeaderForStream(
    Http2StreamId stream_id, absl::string_view key, absl::string_view value) {
  QUICHE_DVLOG(2) << ENDPOINT << "Stream " << stream_id << " received header "
                  << key << " = " << value;
  GetOrCreateH2Stream(stream_id)->received_headers.AppendValueOrAddHeader(
      key, value);
  return OnHeaderResult::HEADER_OK;
}

bool MasqueH2Connection::AttemptToSend() {
  if (!tls_write_buffer_.empty()) {
    QUICHE_DVLOG(1) << ENDPOINT << "Attempting to write "
                    << tls_write_buffer_.size() << " buffered bytes to TLS";
    if (!WriteDataToTls("")) {
      return false;
    }
  }
  if (!h2_adapter_) {
    return false;
  }
  int h2_send_result = h2_adapter_->Send();
  if (h2_send_result != 0) {
    Abort(absl::AbortedError("h2 adapter failed to send"));
    return false;
  }
  return true;
}

bool MasqueH2Connection::OnEndHeadersForStream(Http2StreamId stream_id) {
  MasqueH2Stream* stream = GetOrCreateH2Stream(stream_id);
  QUICHE_LOG(INFO) << ENDPOINT << "OnEndHeadersForStream " << stream_id
                   << " headers: " << stream->received_headers.DebugString();
  return true;
}

void MasqueH2Connection::SendResponse(int32_t stream_id,
                                      const quiche::HttpHeaderBlock& headers,
                                      const std::string& body) {
  MasqueH2Stream* stream = GetOrCreateH2Stream(stream_id);
  std::vector<Header> h2_headers = ConvertHeaders(headers);
  stream->body_to_send = body;
  if (h2_adapter_->SubmitResponse(
          stream_id, h2_headers,
          /*end_stream=*/stream->body_to_send.empty()) != 0) {
    Abort(absl::InternalError(
        absl::StrCat("Failed to submit response for stream ", stream_id,
                     " with body of length ", body.size(),
                     ", headers: ", headers.DebugString())));
  }
  QUICHE_LOG(INFO) << ENDPOINT << "Sending response on stream ID " << stream_id
                   << " with body of length " << body.size()
                   << ", headers: " << headers.DebugString();
  if (!body.empty()) {
    QUICHE_DVLOG(2) << ENDPOINT << "Body to be sent:" << std::endl
                    << quiche::QuicheTextUtils::HexDump(body);
  }
}

int32_t MasqueH2Connection::SendRequest(const quiche::HttpHeaderBlock& headers,
                                        const std::string& body) {
  if (is_server_) {
    QUICHE_LOG(FATAL) << "Server cannot send requests";
  }
  if (!h2_adapter_) {
    QUICHE_LOG(ERROR) << "Connection is not ready to send requests yet";
    return -1;
  }
  std::vector<Header> h2_headers = ConvertHeaders(headers);
  int32_t stream_id =
      h2_adapter_->SubmitRequest(h2_headers, /*end_stream=*/body.empty(),
                                 /*user_data=*/nullptr);
  if (stream_id < 0) {
    Abort(absl::InvalidArgumentError(
        absl::StrCat("Failed to submit request with body of length ",
                     body.size(), ", headers: ", headers.DebugString())));
    return -1;
  }
  QUICHE_LOG(INFO) << ENDPOINT << "Sending request on stream ID " << stream_id
                   << " with body of length " << body.size()
                   << ", headers: " << headers.DebugString();
  if (!body.empty()) {
    QUICHE_DVLOG(2) << ENDPOINT << "Body to be sent:" << std::endl
                    << quiche::QuicheTextUtils::HexDump(body);
  }
  GetOrCreateH2Stream(stream_id)->body_to_send = body;
  return stream_id;
}

std::vector<Header> MasqueH2Connection::ConvertHeaders(
    const quiche::HttpHeaderBlock& headers) {
  std::vector<Header> h2_headers;
  for (const auto& [key, value] : headers) {
    h2_headers.push_back({http2::adapter::HeaderRep(std::string(key)),
                          http2::adapter::HeaderRep(std::string(value))});
  }
  return h2_headers;
}

bool MasqueH2Connection::OnBeginDataForStream(Http2StreamId stream_id,
                                              size_t payload_length) {
  QUICHE_DVLOG(1) << ENDPOINT << "OnBeginDataForStream " << stream_id
                  << " payload_length: " << payload_length;
  return true;
}

bool MasqueH2Connection::OnDataPaddingLength(Http2StreamId stream_id,
                                             size_t padding_length) {
  QUICHE_DVLOG(1) << ENDPOINT << "OnDataPaddingLength stream_id: " << stream_id
                  << " padding_length: " << padding_length;
  return true;
}

bool MasqueH2Connection::OnDataForStream(Http2StreamId stream_id,
                                         absl::string_view data) {
  GetOrCreateH2Stream(stream_id)->received_body.append(data);
  QUICHE_DVLOG(1) << ENDPOINT << "OnDataForStream " << stream_id
                  << " new data length: " << data.size() << " total length: "
                  << GetOrCreateH2Stream(stream_id)->received_body.size();
  return true;
}

bool MasqueH2Connection::OnEndStream(Http2StreamId stream_id) {
  MasqueH2Stream* stream = GetOrCreateH2Stream(stream_id);
  QUICHE_LOG(INFO) << ENDPOINT << "Received END_STREAM for stream " << stream_id
                   << " body length: " << stream->received_body.size();
  QUICHE_DVLOG(2) << ENDPOINT << "Body: " << std::endl
                  << quiche::QuicheTextUtils::HexDump(stream->received_body);
  if (!stream->callback_fired) {
    stream->callback_fired = true;
    if (is_server_) {
      visitor_->OnRequest(this, stream_id, stream->received_headers,
                          stream->received_body);
    } else {
      visitor_->OnResponse(this, stream_id, stream->received_headers,
                           stream->received_body);
    }
  }
  return true;
}

void MasqueH2Connection::OnRstStream(Http2StreamId stream_id,
                                     Http2ErrorCode error_code) {
  QUICHE_LOG(INFO) << ENDPOINT << "Stream " << stream_id
                   << " reset with error code "
                   << Http2ErrorCodeToString(error_code);
  auto it = h2_streams_.find(stream_id);
  if (it != h2_streams_.end()) {
    if (!it->second->callback_fired) {
      it->second->callback_fired = true;
      visitor_->OnStreamFailure(
          this, stream_id,
          absl::InvalidArgumentError(
              absl::StrCat("Stream ", stream_id, " reset with error code ",
                           Http2ErrorCodeToString(error_code))));
    }
  }
}

bool MasqueH2Connection::OnCloseStream(Http2StreamId stream_id,
                                       Http2ErrorCode error_code) {
  QUICHE_LOG(INFO) << ENDPOINT << "Stream " << stream_id
                   << " closed with error code "
                   << Http2ErrorCodeToString(error_code);
  auto it = h2_streams_.find(stream_id);
  if (it != h2_streams_.end()) {
    if (!it->second->callback_fired) {
      visitor_->OnStreamFailure(
          this, stream_id,
          absl::InternalError(
              absl::StrCat("Stream ", stream_id, " closed with error code ",
                           Http2ErrorCodeToString(error_code))));
    }
    h2_streams_.erase(it);
  }
  return true;
}

void MasqueH2Connection::OnPriorityForStream(Http2StreamId stream_id,
                                             Http2StreamId parent_stream_id,
                                             int weight, bool exclusive) {
  QUICHE_LOG(INFO) << ENDPOINT << "Stream " << stream_id
                   << " received priority " << weight
                   << (exclusive ? " exclusive" : "") << " parent "
                   << parent_stream_id;
}

void MasqueH2Connection::OnPing(Http2PingId ping_id, bool is_ack) {
  QUICHE_LOG(INFO) << ENDPOINT << "Received ping " << ping_id
                   << (is_ack ? " ack" : "");
}

void MasqueH2Connection::OnPushPromiseForStream(
    Http2StreamId stream_id, Http2StreamId promised_stream_id) {
  QUICHE_LOG(INFO) << ENDPOINT << "Stream " << stream_id
                   << " received push promise for stream "
                   << promised_stream_id;
}

bool MasqueH2Connection::OnGoAway(Http2StreamId last_accepted_stream_id,
                                  Http2ErrorCode error_code,
                                  absl::string_view opaque_data) {
  QUICHE_LOG(INFO) << ENDPOINT
                   << "Received GOAWAY frame with last_accepted_stream_id: "
                   << last_accepted_stream_id
                   << " error_code: " << Http2ErrorCodeToString(error_code)
                   << " opaque_data length: " << opaque_data.size();
  return true;
}

void MasqueH2Connection::OnWindowUpdate(Http2StreamId stream_id,
                                        int window_increment) {
  QUICHE_LOG(INFO) << ENDPOINT << "Stream " << stream_id
                   << " received window update " << window_increment;
}

int MasqueH2Connection::OnBeforeFrameSent(uint8_t frame_type,
                                          Http2StreamId stream_id,
                                          size_t length, uint8_t flags) {
  QUICHE_DVLOG(1) << ENDPOINT << "OnBeforeFrameSent frame_type: "
                  << static_cast<int>(frame_type) << " stream_id: " << stream_id
                  << " length: " << length
                  << " flags: " << static_cast<int>(flags);
  return 0;
}

int MasqueH2Connection::OnFrameSent(uint8_t frame_type, Http2StreamId stream_id,
                                    size_t length, uint8_t flags,
                                    uint32_t error_code) {
  QUICHE_DVLOG(1) << ENDPOINT
                  << "OnFrameSent frame_type: " << static_cast<int>(frame_type)
                  << " stream_id: " << stream_id << " length: " << length
                  << " flags: " << static_cast<int>(flags)
                  << " error_code: " << error_code;
  return 0;
}

bool MasqueH2Connection::OnInvalidFrame(Http2StreamId stream_id,
                                        InvalidFrameError error) {
  QUICHE_LOG(INFO) << ENDPOINT << "Stream " << stream_id
                   << " received invalid frame error "
                   << http2::adapter::InvalidFrameErrorToString(error);
  return true;
}

void MasqueH2Connection::OnBeginMetadataForStream(Http2StreamId stream_id,
                                                  size_t payload_length) {
  QUICHE_LOG(INFO) << ENDPOINT << "Stream " << stream_id
                   << " about to receive metadata of length " << payload_length;
}

bool MasqueH2Connection::OnMetadataForStream(Http2StreamId stream_id,
                                             absl::string_view metadata) {
  QUICHE_LOG(INFO) << ENDPOINT << "Stream " << stream_id
                   << " received metadata of length " << metadata.size();
  return true;
}

bool MasqueH2Connection::OnMetadataEndForStream(Http2StreamId stream_id) {
  QUICHE_LOG(INFO) << ENDPOINT << "Stream " << stream_id
                   << " done receiving metadata";
  return true;
}

void MasqueH2Connection::OnErrorDebug(absl::string_view message) {
  QUICHE_LOG(ERROR) << ENDPOINT << "OnErrorDebug: " << message;
}

MasqueH2Connection::MasqueH2Stream* MasqueH2Connection::GetOrCreateH2Stream(
    Http2StreamId stream_id) {
  auto it = h2_streams_.find(stream_id);
  if (it != h2_streams_.end()) {
    return it->second.get();
  }
  return h2_streams_.insert({stream_id, std::make_unique<MasqueH2Stream>()})
      .first->second.get();
}

}  // namespace quic

#undef ENDPOINT
