// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_H2_CONNECTION_H_
#define QUICHE_QUIC_MASQUE_MASQUE_H2_CONNECTION_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/http2/adapter/oghttp2_adapter.h"
#include "openssl/base.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quic {

class QUICHE_NO_EXPORT MasqueH2Connection
    : public http2::adapter::Http2VisitorInterface {
 public:
  using Http2ErrorCode = http2::adapter::Http2ErrorCode;
  using Http2PingId = http2::adapter::Http2PingId;
  using Http2Setting = http2::adapter::Http2Setting;
  using Http2StreamId = http2::adapter::Http2StreamId;
  using http2::adapter::Http2VisitorInterface::ConnectionError;
  using http2::adapter::Http2VisitorInterface::DataFrameHeaderInfo;
  using http2::adapter::Http2VisitorInterface::InvalidFrameError;
  using http2::adapter::Http2VisitorInterface::OnHeaderResult;

  class QUICHE_NO_EXPORT Visitor {
   public:
    virtual ~Visitor() = default;
    virtual void OnConnectionReady(MasqueH2Connection *connection) = 0;
    virtual void OnConnectionFinished(MasqueH2Connection *connection) = 0;
    virtual void OnRequest(MasqueH2Connection *connection, int32_t stream_id,
                           const quiche::HttpHeaderBlock &headers,
                           const std::string &body) = 0;
    virtual void OnResponse(MasqueH2Connection *connection, int32_t stream_id,
                            const quiche::HttpHeaderBlock &headers,
                            const std::string &body) = 0;
  };

  // `ssl` and `visitor` must outlive this object.
  explicit MasqueH2Connection(SSL *ssl, bool is_server, Visitor *visitor);

  MasqueH2Connection(const MasqueH2Connection &) = delete;
  MasqueH2Connection(MasqueH2Connection &&) = delete;
  MasqueH2Connection &operator=(const MasqueH2Connection &) = delete;
  MasqueH2Connection &operator=(MasqueH2Connection &&) = delete;

  ~MasqueH2Connection();

  bool aborted() const { return aborted_; }
  // Call when there is more data to be read from SSL.
  void OnTransportReadable();
  // Call when there is more data to be written to SSL.
  bool AttemptToSend();
  int32_t SendRequest(const quiche::HttpHeaderBlock &headers,
                      const std::string &body);
  void SendResponse(int32_t stream_id, const quiche::HttpHeaderBlock &headers,
                    const std::string &body);

 private:
  struct MasqueH2Stream {
    quiche::HttpHeaderBlock received_headers;
    std::string received_body;
    std::string body_to_send;
  };
  static constexpr size_t kBioBufferSize = 16384;
  void Abort();
  void StartH2();
  bool TryRead();
  MasqueH2Stream *GetOrCreateH2Stream(Http2StreamId stream_id);
  std::vector<http2::adapter::Header> ConvertHeaders(
      const quiche::HttpHeaderBlock &headers);

  int WriteDataToTls(absl::string_view data);

  // From http2::adapter::Http2VisitorInterface.
  int64_t OnReadyToSend(absl::string_view serialized) override;
  DataFrameHeaderInfo OnReadyToSendDataForStream(Http2StreamId stream_id,
                                                 size_t max_length) override;
  bool SendDataFrame(Http2StreamId stream_id, absl::string_view frame_header,
                     size_t payload_bytes) override;
  void OnConnectionError(ConnectionError error) override;
  void OnSettingsStart() override;
  void OnSetting(Http2Setting setting) override;
  void OnSettingsEnd() override;
  void OnSettingsAck() override;
  bool OnBeginHeadersForStream(Http2StreamId stream_id) override;
  OnHeaderResult OnHeaderForStream(Http2StreamId stream_id,
                                   absl::string_view key,
                                   absl::string_view value) override;
  bool OnEndHeadersForStream(Http2StreamId stream_id) override;
  bool OnBeginDataForStream(Http2StreamId stream_id,
                            size_t payload_length) override;
  bool OnDataPaddingLength(Http2StreamId stream_id,
                           size_t padding_length) override;
  bool OnDataForStream(Http2StreamId stream_id,
                       absl::string_view data) override;
  bool OnEndStream(Http2StreamId stream_id) override;
  void OnRstStream(Http2StreamId stream_id, Http2ErrorCode error_code) override;
  bool OnCloseStream(Http2StreamId stream_id,
                     Http2ErrorCode error_code) override;
  void OnPriorityForStream(Http2StreamId stream_id,
                           Http2StreamId parent_stream_id, int weight,
                           bool exclusive) override;
  void OnPing(Http2PingId ping_id, bool is_ack) override;
  void OnPushPromiseForStream(Http2StreamId stream_id,
                              Http2StreamId promised_stream_id) override;
  bool OnGoAway(Http2StreamId last_accepted_stream_id,
                Http2ErrorCode error_code,
                absl::string_view opaque_data) override;
  void OnWindowUpdate(Http2StreamId stream_id, int window_increment) override;
  int OnBeforeFrameSent(uint8_t frame_type, Http2StreamId stream_id,
                        size_t length, uint8_t flags) override;
  int OnFrameSent(uint8_t frame_type, Http2StreamId stream_id, size_t length,
                  uint8_t flags, uint32_t error_code) override;
  bool OnInvalidFrame(Http2StreamId stream_id,
                      InvalidFrameError error) override;
  void OnBeginMetadataForStream(Http2StreamId stream_id,
                                size_t payload_length) override;
  bool OnMetadataForStream(Http2StreamId stream_id,
                           absl::string_view metadata) override;
  bool OnMetadataEndForStream(Http2StreamId stream_id) override;
  void OnErrorDebug(absl::string_view message) override;

  SSL *ssl_;
  std::unique_ptr<http2::adapter::OgHttp2Adapter> h2_adapter_;
  const bool is_server_;
  bool tls_connected_ = false;
  bool aborted_ = false;
  absl::flat_hash_map<Http2StreamId, std::unique_ptr<MasqueH2Stream>>
      h2_streams_;
  Visitor *visitor_;
};

// Logs an SSL error that was provided by BoringSSL.
void PrintSSLError(const char *msg, int ssl_err, int ret);

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_H2_CONNECTION_H_
