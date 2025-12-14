// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_QBONE_SESSION_BASE_H_
#define QUICHE_QUIC_QBONE_QBONE_SESSION_BASE_H_

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_crypto_server_stream_base.h"
#include "quiche/quic/core/quic_crypto_stream.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/qbone/qbone_packet_writer.h"
#include "quiche/quic/qbone/qbone_stream.h"

namespace quic {

class QUIC_EXPORT_PRIVATE QboneSessionBase : public QuicSession {
 public:
  QboneSessionBase(QuicConnection* connection, Visitor* owner,
                   const QuicConfig& config,
                   const ParsedQuicVersionVector& supported_versions,
                   QbonePacketWriter* writer);
  QboneSessionBase(const QboneSessionBase&) = delete;
  QboneSessionBase& operator=(const QboneSessionBase&) = delete;
  ~QboneSessionBase() override;

  // Overrides from QuicSession.
  // This will ensure that the crypto session is created.
  void Initialize() override;
  // This will check if the packet is wholly contained.
  void OnStreamFrame(const QuicStreamFrame& frame) override;
  // Called whenever a DATAGRAM frame is received.
  void OnDatagramReceived(absl::string_view datagram) override;

  virtual void ProcessPacketFromNetwork(absl::string_view packet) = 0;
  virtual void ProcessPacketFromPeer(absl::string_view packet) = 0;

  // Returns the number of QBONE network packets that were received
  // that fit into a single QuicStreamFrame and elided the creation of
  // a QboneReadOnlyStream.
  uint64_t GetNumEphemeralPackets() const;

  // Returns the number of QBONE network packets that were via
  // multiple packets, requiring the creation of a QboneReadOnlyStream.
  uint64_t GetNumStreamedPackets() const;

  // Returns the number of QBONE network packets that were received using QUIC
  // DATAGRAM frame.
  uint64_t GetNumDatagramPackets() const;

  // Returns the number of times sending a DATAGRAM frame failed, and the
  // session used an ephemeral stream instead.
  uint64_t GetNumFallbackToStream() const;

  void set_writer(QbonePacketWriter* writer);
  void set_send_packets_as_datagrams(bool send_packets_as_datagrams) {
    send_packets_as_datagrams_ = send_packets_as_datagrams;
  }

 protected:
  virtual std::unique_ptr<QuicCryptoStream> CreateCryptoStream() = 0;

  // QuicSession interface implementation.
  QuicCryptoStream* GetMutableCryptoStream() override;
  const QuicCryptoStream* GetCryptoStream() const override;
  QuicStream* CreateIncomingStream(QuicStreamId id) override;
  QuicStream* CreateIncomingStream(PendingStream* pending) override;
  bool ShouldKeepConnectionAlive() const override;

  bool MaybeIncreaseLargestPeerStreamId(const QuicStreamId stream_id) override {
    return true;
  }

  QuicStream* CreateOutgoingStream();
  std::unique_ptr<QuicStream> CreateDataStream(QuicStreamId id);
  // Activates a QuicStream.  The session takes ownership of the stream, but
  // returns an unowned pointer to the stream for convenience.
  QuicStream* ActivateDataStream(std::unique_ptr<QuicStream> stream);

  // Accepts a given packet from the network and writes it out
  // to the QUIC stream. This will create an ephemeral stream per
  // packet. This function will return true if a stream was created
  // and the packet sent. It will return false if the stream could not
  // be created.
  void SendPacketToPeer(absl::string_view packet);

  QbonePacketWriter* writer_;

  // If true, send QUIC DATAGRAM (aka DATAGRAM) frames instead of ephemeral
  // streams. Note that receiving DATAGRAM frames is always supported.
  bool send_packets_as_datagrams_ = true;

 private:
  // Used for the crypto handshake.
  std::unique_ptr<QuicCryptoStream> crypto_stream_;

  // Statistics for the packets received by the session.
  uint64_t num_ephemeral_packets_ = 0;
  uint64_t num_datagram_packets_ = 0;
  uint64_t num_streamed_packets_ = 0;

  // Number of times the connection has failed to send packets as DATAGRAM frame
  // and used streams as a fallback.
  uint64_t num_fallback_to_stream_ = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_QBONE_SESSION_BASE_H_
