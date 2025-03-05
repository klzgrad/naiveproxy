// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A server specific QuicSession subclass.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SERVER_SESSION_BASE_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SERVER_SESSION_BASE_H_

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "quiche/quic/core/crypto/quic_compressed_certs_cache.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

class QuicConfig;
class QuicConnection;
class QuicCryptoServerConfig;

namespace test {
class QuicServerSessionBasePeer;
class QuicSimpleServerSessionPeer;
}  // namespace test

class QUICHE_EXPORT QuicServerSessionBase : public QuicSpdySession {
 public:
  // Does not take ownership of |connection|. |crypto_config| must outlive the
  // session. |helper| must outlive any created crypto streams.
  QuicServerSessionBase(const QuicConfig& config,
                        const ParsedQuicVersionVector& supported_versions,
                        QuicConnection* connection,
                        QuicSession::Visitor* visitor,
                        QuicCryptoServerStreamBase::Helper* helper,
                        const QuicCryptoServerConfig* crypto_config,
                        QuicCompressedCertsCache* compressed_certs_cache);
  QuicServerSessionBase(const QuicServerSessionBase&) = delete;
  QuicServerSessionBase& operator=(const QuicServerSessionBase&) = delete;

  // Override the base class to cancel any ongoing asychronous crypto.
  void OnConnectionClosed(const QuicConnectionCloseFrame& frame,
                          ConnectionCloseSource source) override;

  // Sends a server config update to the client, containing new bandwidth
  // estimate.
  void OnCongestionWindowChange(QuicTime now) override;

  ~QuicServerSessionBase() override;

  void Initialize() override;

  const QuicCryptoServerStreamBase* crypto_stream() const {
    return crypto_stream_.get();
  }

  // Override base class to process bandwidth related config received from
  // client.
  void OnConfigNegotiated() override;

  void set_serving_region(const std::string& serving_region) {
    serving_region_ = serving_region;
  }

  const std::string& serving_region() const { return serving_region_; }

  QuicSSLConfig GetSSLConfig() const override;

 protected:
  // QuicSession methods(override them with return type of QuicSpdyStream*):
  QuicCryptoServerStreamBase* GetMutableCryptoStream() override;

  const QuicCryptoServerStreamBase* GetCryptoStream() const override;

  std::optional<CachedNetworkParameters> GenerateCachedNetworkParameters()
      const override;

  // If an outgoing stream can be created, return true.
  // Return false when connection is closed or forward secure encryption hasn't
  // established yet or number of server initiated streams already reaches the
  // upper limit.
  bool ShouldCreateOutgoingBidirectionalStream() override;
  bool ShouldCreateOutgoingUnidirectionalStream() override;

  // If we should create an incoming stream, returns true. Otherwise
  // does error handling, including communicating the error to the client and
  // possibly closing the connection, and returns false.
  bool ShouldCreateIncomingStream(QuicStreamId id) override;

  virtual std::unique_ptr<QuicCryptoServerStreamBase>
  CreateQuicCryptoServerStream(
      const QuicCryptoServerConfig* crypto_config,
      QuicCompressedCertsCache* compressed_certs_cache) = 0;

  const QuicCryptoServerConfig* crypto_config() { return crypto_config_; }

  QuicCryptoServerStreamBase::Helper* stream_helper() { return helper_; }

 private:
  friend class test::QuicServerSessionBasePeer;
  friend class test::QuicSimpleServerSessionPeer;

  // Informs the QuicCryptoStream of the SETTINGS that will be used on this
  // connection, so that the server crypto stream knows whether to accept 0-RTT
  // data.
  void SendSettingsToCryptoStream();

  const QuicCryptoServerConfig* crypto_config_;

  // The cache which contains most recently compressed certs.
  // Owned by QuicDispatcher.
  QuicCompressedCertsCache* compressed_certs_cache_;

  std::unique_ptr<QuicCryptoServerStreamBase> crypto_stream_;

  // Pointer to the helper used to create crypto server streams. Must outlive
  // streams created via CreateQuicCryptoServerStream.
  QuicCryptoServerStreamBase::Helper* helper_;

  // Whether bandwidth resumption is enabled for this connection.
  bool bandwidth_resumption_enabled_;

  // The most recent bandwidth estimate sent to the client.
  QuicBandwidth bandwidth_estimate_sent_to_client_;

  // Text describing server location. Sent to the client as part of the
  // bandwidth estimate in the source-address token. Optional, can be left
  // empty.
  std::string serving_region_;

  // Time at which we send the last SCUP to the client.
  QuicTime last_scup_time_;

  // Number of packets sent to the peer, at the time we last sent a SCUP.
  QuicPacketNumber last_scup_packet_number_;

  // Converts QuicBandwidth to an int32 bytes/second that can be
  // stored in CachedNetworkParameters.  TODO(jokulik): This function
  // should go away once we fix http://b//27897982
  int32_t BandwidthToCachedParameterBytesPerSecond(
      const QuicBandwidth& bandwidth) const;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SERVER_SESSION_BASE_H_
