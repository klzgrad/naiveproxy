// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_CRYPTO_STREAM_H_
#define QUICHE_QUIC_CORE_QUIC_CRYPTO_STREAM_H_

#include <array>
#include <cstddef>
#include <string>

#include "absl/strings/string_view.h"
#include "openssl/ssl.h"
#include "quiche/quic/core/crypto/crypto_framer.h"
#include "quiche/quic/core/crypto/crypto_utils.h"
#include "quiche/quic/core/proto/cached_network_parameters_proto.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

class CachedNetworkParameters;
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
      QuicTransportVersion version, QuicConnectionId connection_id);

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
  // invalid (e.g. |label| contains null bytes); returns true on success. This
  // method is only supported for IETF QUIC and MUST NOT be called in gQUIC as
  // that'll trigger an assert in DEBUG build.
  virtual bool ExportKeyingMaterial(absl::string_view label,
                                    absl::string_view context,
                                    size_t result_len, std::string* result) = 0;

  // Writes |data| to the QuicStream at level |level|.
  virtual void WriteCryptoData(EncryptionLevel level, absl::string_view data);

  // Returns the ssl_early_data_reason_t describing why 0-RTT was accepted or
  // rejected. Note that the value returned by this function may vary during the
  // handshake. Once |one_rtt_keys_available| returns true, the value returned
  // by this function will not change for the rest of the lifetime of the
  // QuicCryptoStream.
  virtual ssl_early_data_reason_t EarlyDataReason() const = 0;

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

  // Called when a packet of ENCRYPTION_HANDSHAKE gets sent.
  virtual void OnHandshakePacketSent() = 0;

  // Called when a handshake done frame has been received.
  virtual void OnHandshakeDoneReceived() = 0;

  // Called when a new token frame has been received.
  virtual void OnNewTokenReceived(absl::string_view token) = 0;

  // Called to get an address token.
  virtual std::string GetAddressToken(
      const CachedNetworkParameters* cached_network_params) const = 0;

  // Called to validate |token|.
  virtual bool ValidateAddressToken(absl::string_view token) const = 0;

  // Get the last CachedNetworkParameters received from a valid address token.
  virtual const CachedNetworkParameters* PreviousCachedNetworkParams()
      const = 0;

  // Set the CachedNetworkParameters that will be returned by
  // PreviousCachedNetworkParams.
  // TODO(wub): This function is test only, move it to a test only library.
  virtual void SetPreviousCachedNetworkParams(
      CachedNetworkParameters cached_network_params) = 0;

  // Returns current handshake state.
  virtual HandshakeState GetHandshakeState() const = 0;

  // Called to provide the server-side application state that must be checked
  // when performing a 0-RTT TLS resumption.
  //
  // On a client, this may be called at any time; 0-RTT tickets will not be
  // cached until this function is called. When a 0-RTT resumption is attempted,
  // QuicSession::SetApplicationState will be called with the state provided by
  // a call to this function on a previous connection.
  //
  // On a server, this function must be called before commencing the handshake,
  // otherwise 0-RTT tickets will not be issued. On subsequent connections,
  // 0-RTT will be rejected if the data passed into this function does not match
  // the data passed in on the connection where the 0-RTT ticket was issued.
  virtual void SetServerApplicationStateForResumption(
      std::unique_ptr<ApplicationState> state) = 0;

  // Returns the maximum number of bytes that can be buffered at a particular
  // encryption level |level|.
  virtual size_t BufferSizeLimitForLevel(EncryptionLevel level) const;

  // Called to generate a decrypter for the next key phase. Each call should
  // generate the key for phase n+1.
  virtual std::unique_ptr<QuicDecrypter>
  AdvanceKeysAndCreateCurrentOneRttDecrypter() = 0;

  // Called to generate an encrypter for the same key phase of the last
  // decrypter returned by AdvanceKeysAndCreateCurrentOneRttDecrypter().
  virtual std::unique_ptr<QuicEncrypter> CreateCurrentOneRttEncrypter() = 0;

  // Return the SSL struct object created by BoringSSL if the stream is using
  // TLS1.3. Otherwise, return nullptr.
  // This method is used in Envoy.
  virtual SSL* GetSsl() const = 0;

  // Called to cancel retransmission of unencrypted crypto stream data.
  void NeuterUnencryptedStreamData();

  // Called to cancel retransmission of data of encryption |level|.
  void NeuterStreamDataOfEncryptionLevel(EncryptionLevel level);

  // Override to record the encryption level of consumed data.
  void OnStreamDataConsumed(QuicByteCount bytes_consumed) override;

  // Returns whether there are any bytes pending retransmission in CRYPTO
  // frames.
  virtual bool HasPendingCryptoRetransmission() const;

  // Writes any pending CRYPTO frame retransmissions.
  void WritePendingCryptoRetransmission();

  // Override to retransmit lost crypto data with the appropriate encryption
  // level.
  void WritePendingRetransmission() override;

  // Override to send unacked crypto data with the appropriate encryption level.
  bool RetransmitStreamData(QuicStreamOffset offset, QuicByteCount data_length,
                            bool fin, TransmissionType type) override;

  // Sends stream retransmission data at |encryption_level|.
  QuicConsumedData RetransmitStreamDataAtLevel(
      QuicStreamOffset retransmission_offset,
      QuicByteCount retransmission_length, EncryptionLevel encryption_level,
      TransmissionType type);

  // Returns the number of bytes of handshake data that have been received from
  // the peer in either CRYPTO or STREAM frames.
  uint64_t crypto_bytes_read() const;

  // Returns the number of bytes of handshake data that have been received from
  // the peer in CRYPTO frames at a particular encryption level.
  QuicByteCount BytesReadOnLevel(EncryptionLevel level) const;

  // Returns the number of bytes of handshake data that have been sent to
  // the peer in CRYPTO frames at a particular encryption level.
  QuicByteCount BytesSentOnLevel(EncryptionLevel level) const;

  // Writes |data_length| of data of a crypto frame to |writer|. The data
  // written is from the send buffer for encryption level |level| and starts at
  // |offset|.
  bool WriteCryptoFrame(EncryptionLevel level, QuicStreamOffset offset,
                        QuicByteCount data_length, QuicDataWriter* writer);

  // Called when data from a CRYPTO frame is considered lost. The lost data is
  // identified by the encryption level, offset, and length in |crypto_frame|.
  void OnCryptoFrameLost(QuicCryptoFrame* crypto_frame);

  // Called to retransmit any outstanding data in the range indicated by the
  // encryption level, offset, and length in |crypto_frame|. Returns true if all
  // data gets retransmitted.
  bool RetransmitData(QuicCryptoFrame* crypto_frame, TransmissionType type);

  // Called to write buffered crypto frames.
  void WriteBufferedCryptoFrames();

  // Returns true if there is buffered crypto frames.
  bool HasBufferedCryptoFrames() const;

  // Returns true if any portion of the data at encryption level |level|
  // starting at |offset| for |length| bytes is outstanding.
  bool IsFrameOutstanding(EncryptionLevel level, size_t offset,
                          size_t length) const;

  // Returns true if the crypto handshake is still waiting for acks of sent
  // data, and false if all data has been acked.
  bool IsWaitingForAcks() const;

  // Helper method for OnDataAvailable. Calls CryptoMessageParser::ProcessInput
  // with the data available in |sequencer| and |level|, and marks the data
  // passed to ProcessInput as consumed.
  virtual void OnDataAvailableInSequencer(QuicStreamSequencer* sequencer,
                                          EncryptionLevel level);

  QuicStreamSequencer* GetStreamSequencerForPacketNumberSpace(
      PacketNumberSpace packet_number_space) {
    return &substreams_[packet_number_space].sequencer;
  }

  // Called by OnCryptoFrame to check if a CRYPTO frame is received at an
  // expected `level`.
  virtual bool IsCryptoFrameExpectedForEncryptionLevel(
      EncryptionLevel level) const = 0;

  // Called to determine the encryption level to send/retransmit crypto data.
  virtual EncryptionLevel GetEncryptionLevelToSendCryptoDataOfSpace(
      PacketNumberSpace space) const = 0;

 private:
  // Data sent and received in CRYPTO frames is sent at multiple packet number
  // spaces. Some of the state for the single logical crypto stream is split
  // across packet number spaces, and a CryptoSubstream is used to manage that
  // state for a particular packet number space.
  struct QUIC_EXPORT_PRIVATE CryptoSubstream {
    CryptoSubstream(QuicCryptoStream* crypto_stream);

    QuicStreamSequencer sequencer;
    QuicStreamSendBuffer send_buffer;
  };

  // Consumed data according to encryption levels.
  // TODO(fayang): This is not needed once switching from QUIC crypto to
  // TLS 1.3, which never encrypts crypto data.
  QuicIntervalSet<QuicStreamOffset> bytes_consumed_[NUM_ENCRYPTION_LEVELS];

  // Keeps state for data sent/received in CRYPTO frames at each packet number
  // space;
  std::array<CryptoSubstream, NUM_PACKET_NUMBER_SPACES> substreams_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_CRYPTO_STREAM_H_
