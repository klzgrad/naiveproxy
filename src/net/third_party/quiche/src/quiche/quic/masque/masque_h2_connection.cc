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

using http2::adapter::Header;
using http2::adapter::Http2KnownSettingsId;

namespace quic {

MasqueH2Connection::MasqueH2Connection(SSL *ssl, bool is_server,
                                       Visitor *visitor)
    : ssl_(ssl), is_server_(is_server), visitor_(visitor) {}

void MasqueH2Connection::OnTransportReadable() {
  while (TryRead()) {
  }
}

MasqueH2Connection::~MasqueH2Connection() {}

void MasqueH2Connection::Abort() {
  if (aborted_) {
    return;
  }
  aborted_ = true;
  QUICHE_LOG(ERROR) << "Aborting connection";
  visitor_->OnConnectionFinished(this);
}

void MasqueH2Connection::StartH2() {
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
        QUICHE_DVLOG(1) << "SSL_do_handshake will require another read";
        return false;
      }
      PrintSSLError("Error while connecting", ssl_err, ssl_handshake_ret);
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
    PrintSSLError("Error while connecting", ssl_err, ssl_read_ret);
    return false;
  }
  if (ssl_read_ret == 0) {
    QUICHE_LOG(INFO) << "TLS read closed";
    return false;
  }
  QUICHE_DVLOG(1) << "Read " << ssl_read_ret << " bytes from TLS";
  QUICHE_DVLOG(2) << "Read TLS bytes:" << std::endl
                  << quiche::QuicheTextUtils::HexDump(absl::string_view(
                         reinterpret_cast<const char *>(buffer), ssl_read_ret));
  h2_adapter_->ProcessBytes(
      absl::string_view(reinterpret_cast<const char *>(buffer), ssl_read_ret));
  return AttemptToSend();
}

int MasqueH2Connection::WriteDataToTls(absl::string_view data) {
  QUICHE_DVLOG(2) << "Writing " << data.size()
                  << " app bytes to TLS:" << std::endl
                  << quiche::QuicheTextUtils::HexDump(data);
  int ssl_write_ret = SSL_write(ssl_, data.data(), data.size());
  if (ssl_write_ret <= 0) {
    int ssl_err = SSL_get_error(ssl_, ssl_write_ret);
    PrintSSLError("Error while writing request to TLS", ssl_err, ssl_write_ret);
    return -1;
  } else {
    if (ssl_write_ret == static_cast<int>(data.size())) {
      QUICHE_DVLOG(1) << "Wrote " << data.size() << " bytes to TLS";
    } else {
      QUICHE_DVLOG(1) << "Wrote " << ssl_write_ret << " / " << data.size()
                      << "bytes to TLS";
    }
  }
  return ssl_write_ret;
}

int64_t MasqueH2Connection::OnReadyToSend(absl::string_view serialized) {
  QUICHE_DVLOG(1) << "Writing " << serialized.size()
                  << " bytes of h2 data to TLS";
  int write_res = WriteDataToTls(serialized);
  if (write_res < 0) {
    return kSendError;
  }
  return write_res;
}

MasqueH2Connection::DataFrameHeaderInfo
MasqueH2Connection::OnReadyToSendDataForStream(Http2StreamId stream_id,
                                               size_t max_length) {
  MasqueH2Stream *stream = GetOrCreateH2Stream(stream_id);
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
  MasqueH2Stream *stream = GetOrCreateH2Stream(stream_id);
  size_t length_to_write = std::min(payload_bytes, stream->body_to_send.size());
  int length_written =
      WriteDataToTls(stream->body_to_send.substr(0, length_to_write));
  if (length_written < 0) {
    return false;
  }
  if (length_written == static_cast<int>(stream->body_to_send.size())) {
    stream->body_to_send.clear();
  } else {
    // Remove the written bytes from the start of `body_to_send`.
    stream->body_to_send = stream->body_to_send.substr(length_written);
  }
  return true;
}

void MasqueH2Connection::OnConnectionError(ConnectionError error) {
  QUICHE_LOG(ERROR) << "OnConnectionError: "
                    << http2::adapter::ConnectionErrorToString(error);
  Abort();
}

void MasqueH2Connection::OnSettingsStart() {}

void MasqueH2Connection::OnSetting(Http2Setting setting) {
  QUICHE_LOG(INFO) << "Received "
                   << http2::adapter::Http2SettingsIdToString(setting.id)
                   << " = " << setting.value;
}

void MasqueH2Connection::OnSettingsEnd() {}
void MasqueH2Connection::OnSettingsAck() {}

bool MasqueH2Connection::OnBeginHeadersForStream(Http2StreamId stream_id) {
  QUICHE_DVLOG(1) << "OnBeginHeadersForStream " << stream_id;
  return true;
}

MasqueH2Connection::OnHeaderResult MasqueH2Connection::OnHeaderForStream(
    Http2StreamId stream_id, absl::string_view key, absl::string_view value) {
  QUICHE_DVLOG(2) << "Stream " << stream_id << " received header " << key
                  << " = " << value;
  GetOrCreateH2Stream(stream_id)->received_headers.AppendValueOrAddHeader(
      key, value);
  return OnHeaderResult::HEADER_OK;
}

bool MasqueH2Connection::AttemptToSend() {
  if (!h2_adapter_) {
    QUICHE_LOG(ERROR) << "Connection is not ready to send yet";
    return false;
  }
  int h2_send_result = h2_adapter_->Send();
  if (h2_send_result != 0) {
    QUICHE_LOG(ERROR) << "h2 adapter failed to send";
    Abort();
    return false;
  }
  return true;
}

bool MasqueH2Connection::OnEndHeadersForStream(Http2StreamId stream_id) {
  MasqueH2Stream *stream = GetOrCreateH2Stream(stream_id);
  QUICHE_LOG(INFO) << "OnEndHeadersForStream " << stream_id
                   << " headers: " << stream->received_headers.DebugString();
  return true;
}

void MasqueH2Connection::SendResponse(int32_t stream_id,
                                      const quiche::HttpHeaderBlock &headers,
                                      const std::string &body) {
  MasqueH2Stream *stream = GetOrCreateH2Stream(stream_id);
  std::vector<Header> h2_headers = ConvertHeaders(headers);
  stream->body_to_send = body;
  if (h2_adapter_->SubmitResponse(
          stream_id, h2_headers,
          /*end_stream=*/stream->body_to_send.empty()) != 0) {
    QUICHE_LOG(ERROR) << "Failed to submit response for stream " << stream_id;
    Abort();
  }
}

int32_t MasqueH2Connection::SendRequest(const quiche::HttpHeaderBlock &headers,
                                        const std::string &body) {
  if (is_server_) {
    QUICHE_LOG(FATAL) << "Server cannot send requests";
  }
  if (!h2_adapter_) {
    QUICHE_LOG(ERROR) << "Connection is not ready to send requests yet";
    return -1;
  }
  std::vector<Header> h2_headers = ConvertHeaders(headers);
  QUICHE_LOG(INFO) << "Sending request with body of length " << body.size()
                   << ", headers: " << headers.DebugString();
  int32_t stream_id =
      h2_adapter_->SubmitRequest(h2_headers, /*end_stream=*/body.empty(),
                                 /*user_data=*/nullptr);
  if (stream_id < 0) {
    QUICHE_LOG(ERROR) << "Failed to submit request";
    Abort();
    return -1;
  }
  GetOrCreateH2Stream(stream_id)->body_to_send = body;
  return stream_id;
}

std::vector<Header> MasqueH2Connection::ConvertHeaders(
    const quiche::HttpHeaderBlock &headers) {
  std::vector<Header> h2_headers;
  for (const auto &[key, value] : headers) {
    h2_headers.push_back({http2::adapter::HeaderRep(std::string(key)),
                          http2::adapter::HeaderRep(std::string(value))});
  }
  return h2_headers;
}

bool MasqueH2Connection::OnBeginDataForStream(Http2StreamId stream_id,
                                              size_t payload_length) {
  QUICHE_DVLOG(1) << "OnBeginDataForStream " << stream_id
                  << " payload_length: " << payload_length;
  return true;
}

bool MasqueH2Connection::OnDataPaddingLength(Http2StreamId stream_id,
                                             size_t padding_length) {
  QUICHE_DVLOG(1) << "OnDataPaddingLength stream_id: " << stream_id
                  << " padding_length: " << padding_length;
  return true;
}

bool MasqueH2Connection::OnDataForStream(Http2StreamId stream_id,
                                         absl::string_view data) {
  QUICHE_DVLOG(1) << "OnDataForStream " << stream_id
                  << " data length: " << data.size();
  GetOrCreateH2Stream(stream_id)->received_body.append(data);
  return true;
}

bool MasqueH2Connection::OnEndStream(Http2StreamId stream_id) {
  MasqueH2Stream *stream = GetOrCreateH2Stream(stream_id);
  QUICHE_LOG(INFO) << "Received END_STREAM for stream " << stream_id
                   << " body length: " << stream->received_body.size()
                   << std::endl
                   << stream->received_body;
  if (is_server_) {
    visitor_->OnRequest(this, stream_id, stream->received_headers,
                        stream->received_body);
  } else {
    visitor_->OnResponse(this, stream_id, stream->received_headers,
                         stream->received_body);
  }
  return true;
}

void MasqueH2Connection::OnRstStream(Http2StreamId stream_id,
                                     Http2ErrorCode error_code) {
  QUICHE_LOG(INFO) << "Stream " << stream_id << " reset with error code "
                   << Http2ErrorCodeToString(error_code);
}

bool MasqueH2Connection::OnCloseStream(Http2StreamId stream_id,
                                       Http2ErrorCode error_code) {
  QUICHE_LOG(INFO) << "Stream " << stream_id << " closed with error code "
                   << Http2ErrorCodeToString(error_code);
  h2_streams_.erase(stream_id);
  return true;
}

void MasqueH2Connection::OnPriorityForStream(Http2StreamId stream_id,
                                             Http2StreamId parent_stream_id,
                                             int weight, bool exclusive) {
  QUICHE_LOG(INFO) << "Stream " << stream_id << " received priority " << weight
                   << (exclusive ? " exclusive" : "") << " parent "
                   << parent_stream_id;
}

void MasqueH2Connection::OnPing(Http2PingId ping_id, bool is_ack) {
  QUICHE_LOG(INFO) << "Received ping " << ping_id << (is_ack ? " ack" : "");
}

void MasqueH2Connection::OnPushPromiseForStream(
    Http2StreamId stream_id, Http2StreamId promised_stream_id) {
  QUICHE_LOG(INFO) << "Stream " << stream_id
                   << " received push promise for stream "
                   << promised_stream_id;
}

bool MasqueH2Connection::OnGoAway(Http2StreamId last_accepted_stream_id,
                                  Http2ErrorCode error_code,
                                  absl::string_view opaque_data) {
  QUICHE_LOG(INFO) << "Received GOAWAY frame with last_accepted_stream_id: "
                   << last_accepted_stream_id
                   << " error_code: " << Http2ErrorCodeToString(error_code)
                   << " opaque_data length: " << opaque_data.size();
  return true;
}

void MasqueH2Connection::OnWindowUpdate(Http2StreamId stream_id,
                                        int window_increment) {
  QUICHE_LOG(INFO) << "Stream " << stream_id << " received window update "
                   << window_increment;
}

int MasqueH2Connection::OnBeforeFrameSent(uint8_t frame_type,
                                          Http2StreamId stream_id,
                                          size_t length, uint8_t flags) {
  QUICHE_DVLOG(1) << "OnBeforeFrameSent frame_type: "
                  << static_cast<int>(frame_type) << " stream_id: " << stream_id
                  << " length: " << length
                  << " flags: " << static_cast<int>(flags);
  return 0;
}

int MasqueH2Connection::OnFrameSent(uint8_t frame_type, Http2StreamId stream_id,
                                    size_t length, uint8_t flags,
                                    uint32_t error_code) {
  QUICHE_DVLOG(1) << "OnFrameSent frame_type: " << static_cast<int>(frame_type)
                  << " stream_id: " << stream_id << " length: " << length
                  << " flags: " << static_cast<int>(flags)
                  << " error_code: " << error_code;
  return 0;
}

bool MasqueH2Connection::OnInvalidFrame(Http2StreamId stream_id,
                                        InvalidFrameError error) {
  QUICHE_LOG(INFO) << "Stream " << stream_id << " received invalid frame error "
                   << http2::adapter::InvalidFrameErrorToString(error);
  return true;
}

void MasqueH2Connection::OnBeginMetadataForStream(Http2StreamId stream_id,
                                                  size_t payload_length) {
  QUICHE_LOG(INFO) << "Stream " << stream_id
                   << " about to receive metadata of length " << payload_length;
}

bool MasqueH2Connection::OnMetadataForStream(Http2StreamId stream_id,
                                             absl::string_view metadata) {
  QUICHE_LOG(INFO) << "Stream " << stream_id << " received metadata of length "
                   << metadata.size();
  return true;
}

bool MasqueH2Connection::OnMetadataEndForStream(Http2StreamId stream_id) {
  QUICHE_LOG(INFO) << "Stream " << stream_id << " done receiving metadata";
  return true;
}

void MasqueH2Connection::OnErrorDebug(absl::string_view message) {
  QUICHE_LOG(ERROR) << "OnErrorDebug: " << message;
}

MasqueH2Connection::MasqueH2Stream *MasqueH2Connection::GetOrCreateH2Stream(
    Http2StreamId stream_id) {
  auto it = h2_streams_.find(stream_id);
  if (it != h2_streams_.end()) {
    return it->second.get();
  }
  return h2_streams_.insert({stream_id, std::make_unique<MasqueH2Stream>()})
      .first->second.get();
}

void PrintSSLError(const char *msg, int ssl_err, int ret) {
  switch (ssl_err) {
    case SSL_ERROR_SSL:
      QUICHE_LOG(ERROR) << msg << ": "
                        << ERR_reason_error_string(ERR_peek_error());
      break;
    case SSL_ERROR_SYSCALL:
      if (ret == 0) {
        QUICHE_LOG(ERROR) << msg << ": peer closed connection";
      } else {
        QUICHE_LOG(ERROR) << msg << ": " << strerror(errno);
      }
      break;
    case SSL_ERROR_ZERO_RETURN:
      QUICHE_LOG(ERROR) << msg << ": received close_notify";
      break;
    default:
      QUICHE_LOG(ERROR) << msg << ": unexpected error: "
                        << SSL_error_description(ssl_err);
      break;
  }
  ERR_print_errors_fp(stderr);
}

}  // namespace quic
