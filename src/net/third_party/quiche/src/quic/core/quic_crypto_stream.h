// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CRYPTO_STREAM_H_
#define QUICHE_QUIC_CORE_QUIC_CRYPTO_STREAM_H_

#include <array>
#include <cstddef>
#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_framer.h"
#include "net/third_party/quiche/src/quic/core/crypto/crypto_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_config.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

class QuicSession;

// Crypto handshake messages in QUIC take place over a reserved stream with the
// id 1.  Each endpoint (client and server) will allocate an instance of a
// subclass of QuicCryptoStream to send and receive handshake messages.  (In the
// normal 1-RTT handshake, the client will send a client hello, CHLO, message.
// The server will receive this message and respond with a server hello message,
// SHLO.  At this point both sides will have established a crypto context they
// can use to send encrypted messages.
//
// For more details:
// https://docs.google.com/document/d/1g5nIXAIkN_Y-7XJW5K45IblHd_L2f5LTaDUDwvZ5L6g/edit?usp=sharing
class QUIC_EXPORT_PRIVATE QuicCryptoStream : public QuicStream {
 public:
  explicit QuicCryptoStream(QuicSession* session);
  QuicCryptoStream(const QuicCryptoStream&) = delete;
  QuicCryptoStream& operator=(const QuicCryptoStream&) = delete;

  ~QuicCryptoStream() override;

  // Returns the per-packet framing overhead associated with sending a
  // handshake message for |version|.
  static QuicByteCount CryptoMessageFramingOverhead(
      QuicTransportVersion version,
      QuicConnectionId connection_id);

  // QuicStream implementation
  void OnStreamFrame(const QuicStreamFrame& frame) override;
  void OnDataAvailable() override;

  // Called when a CRYPTO frame is received.
  void OnCryptoFrame(const QuicCryptoFrame& frame);

  // Called when a CRYPTO frame is ACKed.
  bool OnCryptoFrameAcked(const QuicCryptoFrame& frame,
                          QuicTime::Delta ack_delay_time);

  void OnStreamReset(const QuicRstStreamFrame& frame) override;

  // Performs key extraction to derive a new secret of |result_len| bytes
  // dependent on |label|, |context|, and the stream's negotiated subkey secret.
  // Returns false if the handshake has not been confirmed or the parameters are
  // invalid (e.g. |label| contains null bytes); returns true on success.
  bool ExportKeyingMaterial(quiche::QuicheStringPiece label,
                            quiche::QuicheStringPiece context,
                            size_t result_len,
                            std::string* result) const;

  // Writes |data| to the QuicStream at level |level|.
  virtual void WriteCryptoData(EncryptionLevel level,
                               quiche::QuicheStringPiece data);

  // Returns true once an encrypter has been set for the connection.
  virtual bool encryption_established() const = 0;

  // Returns true once the crypto handshake has completed.
  virtual bool one_rtt_keys_available() const = 0;

  // Returns the parameters negotiated in the crypto handshake.
  virtual const QuicCryptoNegotiatedParameters& crypto_negotiated_params()
      const = 0;

  // Provides the message parser to use when data is received on this stream.
  virtual CryptoMessageParser* crypto_message_parser() = 0;

  // Called when a packet of encryption |level| has been successfully decrypted.
  virtual void OnPacketDecrypted(EncryptionLevel level) = 0;

  // Called when a 1RTT packet has been acknowledged.
  virtual void OnOneRttPacketAcknowledged() = 0;

  // Called when a handshake done frame has been received.
  virtual void OnHandshakeDoneReceived() = 0;

  // Returns current handshake state.
  virtual HandshakeState GetHandshakeState() const = 0;

  // Returns the maximum number of bytes that can be buffered at a particular
  // encryption level |level|.
  virtual size_t BufferSizeLimitForLevel(EncryptionLevel level) const;

  // Called to cancel retransmission of unencrypted crypto stream data.
  void NeuterUnencryptedStreamData();

  // Override to record the encryption level of consumed data.
  void OnStreamDataConsumed(size_t bytes_consumed) override;

  // Returns whether there are any bytes pending retransmission in CRYPTO
  // frames.
  virtual bool HasPendingCryptoRetransmission() const;

  // Writes any pending CRYPTO frame retransmissions.
  void WritePendingCryptoRetransmission();

  // Override to retransmit lost crypto data with the appropriate encryption
  // level.
  void WritePendingRetransmission() override;

  // Override to send unacked crypto data with the appropriate encryption level.
  bool RetransmitStreamData(QuicStreamOffset offset,
                            QuicByteCount data_length,
                            bool fin,
                            TransmissionType type) override;

  // Sends stream retransmission data at |encryption_level|.
  QuicConsumedData RetransmitStreamDataAtLevel(
      QuicStreamOffset retransmission_offset,
      QuicByteCount retransmission_length,
      EncryptionLevel encryption_level,
      TransmissionType type);

  // Returns the number of bytes of handshake data that have been received from
  // the peer in either CRYPTO or STREAM frames.
  uint64_t crypto_bytes_read() const;

  // Returns the number of bytes of handshake data that have been received from
  // the peer in CRYPTO frames at a particular encryption level.
  QuicByteCount BytesReadOnLevel(EncryptionLevel level) const;

  // Writes |data_length| of data of a crypto frame to |writer|. The data
  // written is from the send buffer for encryption level |level| and starts at
  // |offset|.
  bool WriteCryptoFrame(EncryptionLevel level,
                        QuicStreamOffset offset,
                        QuicByteCount data_length,
                        QuicDataWriter* writer);

  // Called when data from a CRYPTO frame is considered lost. The lost data is
  // identified by the encryption level, offset, and length in |crypto_frame|.
  void OnCryptoFrameLost(QuicCryptoFrame* crypto_frame);

  // Called to retransmit any outstanding data in the range indicated by the
  // encryption level, offset, and length in |crypto_frame|.
  void RetransmitData(QuicCryptoFrame* crypto_frame, TransmissionType type);

  // Called to write buffered crypto frames.
  void WriteBufferedCryptoFrames();

  // Returns true if there is buffered crypto frames.
  bool HasBufferedCryptoFrames() const;

  // Returns true if any portion of the data at encryption level |level|
  // starting at |offset| for |length| bytes is outstanding.
  bool IsFrameOutstanding(EncryptionLevel level,
                          size_t offset,
                          size_t length) const;

  // Returns true if the crypto handshake is still waiting for acks of sent
  // data, and false if all data has been acked.
  bool IsWaitingForAcks() const;

 private:
  // Data sent and received in CRYPTO frames is sent at multiple encryption
  // levels. Some of the state for the single logical crypto stream is split
  // across encryption levels, and a CryptoSubstream is used to manage that
  // state for a particular encryption level.
  struct QUIC_EXPORT_PRIVATE CryptoSubstream {
    CryptoSubstream(QuicCryptoStream* crypto_stream, EncryptionLevel);

    QuicStreamSequencer sequencer;
    QuicStreamSendBuffer send_buffer;
  };

  // Helper method for OnDataAvailable. Calls CryptoMessageParser::ProcessInput
  // with the data available in |sequencer| and |level|, and marks the data
  // passed to ProcessInput as consumed.
  void OnDataAvailableInSequencer(QuicStreamSequencer* sequencer,
                                  EncryptionLevel level);

  // Consumed data according to encryption levels.
  // TODO(fayang): This is not needed once switching from QUIC crypto to
  // TLS 1.3, which never encrypts crypto data.
  QuicIntervalSet<QuicStreamOffset> bytes_consumed_[NUM_ENCRYPTION_LEVELS];

  // Keeps state for data sent/received in CRYPTO frames at each encryption
  // level.
  std::array<CryptoSubstream, NUM_ENCRYPTION_LEVELS> substreams_;

  // Latched value of gfe2_reloadable_flag_quic_writevdata_at_level.
  const bool writevdata_at_level_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CRYPTO_STREAM_H_
