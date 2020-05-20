// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_FRAMER_H_
#define QUICHE_QUIC_CORE_QUIC_FRAMER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/quic_decrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_encrypter.h"
#include "net/third_party/quiche/src/quic/core/crypto/quic_random.h"
#include "net/third_party/quiche/src/quic/core/quic_connection_id.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

namespace test {
class QuicFramerPeer;
}  // namespace test

class QuicDataReader;
class QuicDataWriter;
class QuicFramer;
class QuicStreamFrameDataProducer;

// Number of bytes reserved for the frame type preceding each frame.
const size_t kQuicFrameTypeSize = 1;
// Number of bytes reserved for error code.
const size_t kQuicErrorCodeSize = 4;
// Number of bytes reserved to denote the length of error details field.
const size_t kQuicErrorDetailsLengthSize = 2;

// Maximum number of bytes reserved for stream id.
const size_t kQuicMaxStreamIdSize = 4;
// Maximum number of bytes reserved for byte offset in stream frame.
const size_t kQuicMaxStreamOffsetSize = 8;
// Number of bytes reserved to store payload length in stream frame.
const size_t kQuicStreamPayloadLengthSize = 2;
// Number of bytes to reserve for IQ Error codes (for the Connection Close,
// Application Close, and Reset Stream frames).
const size_t kQuicIetfQuicErrorCodeSize = 2;
// Minimum size of the IETF QUIC Error Phrase's length field
const size_t kIetfQuicMinErrorPhraseLengthSize = 1;

// Size in bytes reserved for the delta time of the largest observed
// packet number in ack frames.
const size_t kQuicDeltaTimeLargestObservedSize = 2;
// Size in bytes reserved for the number of received packets with timestamps.
const size_t kQuicNumTimestampsSize = 1;
// Size in bytes reserved for the number of missing packets in ack frames.
const size_t kNumberOfNackRangesSize = 1;
// Size in bytes reserved for the number of ack blocks in ack frames.
const size_t kNumberOfAckBlocksSize = 1;
// Maximum number of missing packet ranges that can fit within an ack frame.
const size_t kMaxNackRanges = (1 << (kNumberOfNackRangesSize * 8)) - 1;
// Maximum number of ack blocks that can fit within an ack frame.
const size_t kMaxAckBlocks = (1 << (kNumberOfAckBlocksSize * 8)) - 1;

// This class receives callbacks from the framer when packets
// are processed.
class QUIC_EXPORT_PRIVATE QuicFramerVisitorInterface {
 public:
  virtual ~QuicFramerVisitorInterface() {}

  // Called if an error is detected in the QUIC protocol.
  virtual void OnError(QuicFramer* framer) = 0;

  // Called only when |perspective_| is IS_SERVER and the framer gets a
  // packet with version flag true and the version on the packet doesn't match
  // |quic_version_|. The visitor should return true after it updates the
  // version of the |framer_| to |received_version| or false to stop processing
  // this packet.
  virtual bool OnProtocolVersionMismatch(
      ParsedQuicVersion received_version) = 0;

  // Called when a new packet has been received, before it
  // has been validated or processed.
  virtual void OnPacket() = 0;

  // Called when a public reset packet has been parsed but has not yet
  // been validated.
  virtual void OnPublicResetPacket(const QuicPublicResetPacket& packet) = 0;

  // Called only when |perspective_| is IS_CLIENT and a version negotiation
  // packet has been parsed.
  virtual void OnVersionNegotiationPacket(
      const QuicVersionNegotiationPacket& packet) = 0;

  // Called only when |perspective_| is IS_CLIENT and a retry packet has been
  // parsed. |new_connection_id| contains the value of the Source Connection
  // ID field, and |retry_token| contains the value of the Retry Token field.
  // On versions where HasRetryIntegrityTag() is false,
  // |original_connection_id| contains the value of the Original Destination
  // Connection ID field, and both |retry_integrity_tag| and
  // |retry_without_tag| are empty.
  // On versions where HasRetryIntegrityTag() is true,
  // |original_connection_id| is empty, |retry_integrity_tag| contains the
  // value of the Retry Integrity Tag field, and |retry_without_tag| contains
  // the entire RETRY packet except the Retry Integrity Tag field.
  virtual void OnRetryPacket(QuicConnectionId original_connection_id,
                             QuicConnectionId new_connection_id,
                             quiche::QuicheStringPiece retry_token,
                             quiche::QuicheStringPiece retry_integrity_tag,
                             quiche::QuicheStringPiece retry_without_tag) = 0;

  // Called when all fields except packet number has been parsed, but has not
  // been authenticated. If it returns false, framing for this packet will
  // cease.
  virtual bool OnUnauthenticatedPublicHeader(
      const QuicPacketHeader& header) = 0;

  // Called when the unauthenticated portion of the header has been parsed.
  // If OnUnauthenticatedHeader returns false, framing for this packet will
  // cease.
  virtual bool OnUnauthenticatedHeader(const QuicPacketHeader& header) = 0;

  // Called when a packet has been decrypted. |level| is the encryption level
  // of the packet.
  virtual void OnDecryptedPacket(EncryptionLevel level) = 0;

  // Called when the complete header of a packet had been parsed.
  // If OnPacketHeader returns false, framing for this packet will cease.
  virtual bool OnPacketHeader(const QuicPacketHeader& header) = 0;

  // Called when the packet being processed contains multiple IETF QUIC packets,
  // which is due to there being more data after what is covered by the length
  // field. |packet| contains the remaining data which can be processed.
  // Note that this is called when the framer parses the length field, before
  // it attempts to decrypt the first payload. It is the visitor's
  // responsibility to buffer the packet and call ProcessPacket on it
  // after the framer is done parsing the current payload. |packet| does not
  // own its internal buffer, the visitor should make a copy of it.
  virtual void OnCoalescedPacket(const QuicEncryptedPacket& packet) = 0;

  // Called when the packet being processed failed to decrypt.
  // |has_decryption_key| indicates whether the framer knew which decryption
  // key to use for this packet and already had a suitable key.
  virtual void OnUndecryptablePacket(const QuicEncryptedPacket& packet,
                                     EncryptionLevel decryption_level,
                                     bool has_decryption_key) = 0;

  // Called when a StreamFrame has been parsed.
  virtual bool OnStreamFrame(const QuicStreamFrame& frame) = 0;

  // Called when a CRYPTO frame has been parsed.
  virtual bool OnCryptoFrame(const QuicCryptoFrame& frame) = 0;

  // Called when largest acked of an AckFrame has been parsed.
  virtual bool OnAckFrameStart(QuicPacketNumber largest_acked,
                               QuicTime::Delta ack_delay_time) = 0;

  // Called when ack range [start, end) of an AckFrame has been parsed.
  virtual bool OnAckRange(QuicPacketNumber start, QuicPacketNumber end) = 0;

  // Called when a timestamp in the AckFrame has been parsed.
  virtual bool OnAckTimestamp(QuicPacketNumber packet_number,
                              QuicTime timestamp) = 0;

  // Called after the last ack range in an AckFrame has been parsed.
  // |start| is the starting value of the last ack range.
  virtual bool OnAckFrameEnd(QuicPacketNumber start) = 0;

  // Called when a StopWaitingFrame has been parsed.
  virtual bool OnStopWaitingFrame(const QuicStopWaitingFrame& frame) = 0;

  // Called when a QuicPaddingFrame has been parsed.
  virtual bool OnPaddingFrame(const QuicPaddingFrame& frame) = 0;

  // Called when a PingFrame has been parsed.
  virtual bool OnPingFrame(const QuicPingFrame& frame) = 0;

  // Called when a RstStreamFrame has been parsed.
  virtual bool OnRstStreamFrame(const QuicRstStreamFrame& frame) = 0;

  // Called when a ConnectionCloseFrame, of any type, has been parsed.
  virtual bool OnConnectionCloseFrame(
      const QuicConnectionCloseFrame& frame) = 0;

  // Called when a StopSendingFrame has been parsed.
  virtual bool OnStopSendingFrame(const QuicStopSendingFrame& frame) = 0;

  // Called when a PathChallengeFrame has been parsed.
  virtual bool OnPathChallengeFrame(const QuicPathChallengeFrame& frame) = 0;

  // Called when a PathResponseFrame has been parsed.
  virtual bool OnPathResponseFrame(const QuicPathResponseFrame& frame) = 0;

  // Called when a GoAwayFrame has been parsed.
  virtual bool OnGoAwayFrame(const QuicGoAwayFrame& frame) = 0;

  // Called when a WindowUpdateFrame has been parsed.
  virtual bool OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) = 0;

  // Called when a BlockedFrame has been parsed.
  virtual bool OnBlockedFrame(const QuicBlockedFrame& frame) = 0;

  // Called when a NewConnectionIdFrame has been parsed.
  virtual bool OnNewConnectionIdFrame(
      const QuicNewConnectionIdFrame& frame) = 0;

  // Called when a RetireConnectionIdFrame has been parsed.
  virtual bool OnRetireConnectionIdFrame(
      const QuicRetireConnectionIdFrame& frame) = 0;

  // Called when a NewTokenFrame has been parsed.
  virtual bool OnNewTokenFrame(const QuicNewTokenFrame& frame) = 0;

  // Called when a message frame has been parsed.
  virtual bool OnMessageFrame(const QuicMessageFrame& frame) = 0;

  // Called when a handshake done frame has been parsed.
  virtual bool OnHandshakeDoneFrame(const QuicHandshakeDoneFrame& frame) = 0;

  // Called when a packet has been completely processed.
  virtual void OnPacketComplete() = 0;

  // Called to check whether |token| is a valid stateless reset token.
  virtual bool IsValidStatelessResetToken(QuicUint128 token) const = 0;

  // Called when an IETF stateless reset packet has been parsed and validated
  // with the stateless reset token.
  virtual void OnAuthenticatedIetfStatelessResetPacket(
      const QuicIetfStatelessResetPacket& packet) = 0;

  // Called when an IETF MaxStreams frame has been parsed.
  virtual bool OnMaxStreamsFrame(const QuicMaxStreamsFrame& frame) = 0;

  // Called when an IETF StreamsBlocked frame has been parsed.
  virtual bool OnStreamsBlockedFrame(const QuicStreamsBlockedFrame& frame) = 0;
};

// Class for parsing and constructing QUIC packets.  It has a
// QuicFramerVisitorInterface that is called when packets are parsed.
class QUIC_EXPORT_PRIVATE QuicFramer {
 public:
  // Constructs a new framer that installs a kNULL QuicEncrypter and
  // QuicDecrypter for level ENCRYPTION_INITIAL. |supported_versions| specifies
  // the list of supported QUIC versions. |quic_version_| is set to the maximum
  // version in |supported_versions|.
  QuicFramer(const ParsedQuicVersionVector& supported_versions,
             QuicTime creation_time,
             Perspective perspective,
             uint8_t expected_server_connection_id_length);
  QuicFramer(const QuicFramer&) = delete;
  QuicFramer& operator=(const QuicFramer&) = delete;

  virtual ~QuicFramer();

  // Returns true if |version| is a supported transport version.
  bool IsSupportedTransportVersion(const QuicTransportVersion version) const;

  // Returns true if |version| is a supported protocol version.
  bool IsSupportedVersion(const ParsedQuicVersion version) const;

  // Set callbacks to be called from the framer.  A visitor must be set, or
  // else the framer will likely crash.  It is acceptable for the visitor
  // to do nothing.  If this is called multiple times, only the last visitor
  // will be used.
  void set_visitor(QuicFramerVisitorInterface* visitor) { visitor_ = visitor; }

  const ParsedQuicVersionVector& supported_versions() const {
    return supported_versions_;
  }

  QuicTransportVersion transport_version() const {
    return version_.transport_version;
  }

  ParsedQuicVersion version() const { return version_; }

  void set_version(const ParsedQuicVersion version);

  // Does not DCHECK for supported version. Used by tests to set unsupported
  // version to trigger version negotiation.
  void set_version_for_tests(const ParsedQuicVersion version) {
    version_ = version;
  }

  QuicErrorCode error() const { return error_; }

  // Allows enabling or disabling of timestamp processing and serialization.
  void set_process_timestamps(bool process_timestamps) {
    process_timestamps_ = process_timestamps;
  }

  // Pass a UDP packet into the framer for parsing.
  // Return true if the packet was processed successfully. |packet| must be a
  // single, complete UDP packet (not a frame of a packet).  This packet
  // might be null padded past the end of the payload, which will be correctly
  // ignored.
  bool ProcessPacket(const QuicEncryptedPacket& packet);

  // Largest size in bytes of all stream frame fields without the payload.
  static size_t GetMinStreamFrameSize(QuicTransportVersion version,
                                      QuicStreamId stream_id,
                                      QuicStreamOffset offset,
                                      bool last_frame_in_packet,
                                      size_t data_length);
  // Returns the overhead of framing a CRYPTO frame with the specific offset and
  // data length provided, but not counting the size of the data payload.
  static size_t GetMinCryptoFrameSize(QuicStreamOffset offset,
                                      QuicPacketLength data_length);
  static size_t GetMessageFrameSize(QuicTransportVersion version,
                                    bool last_frame_in_packet,
                                    QuicByteCount length);
  // Size in bytes of all ack frame fields without the missing packets or ack
  // blocks.
  // TODO(fayang): Remove |largest_observed_length| when deprecating
  // quic_use_ack_frame_to_get_min_size.
  static size_t GetMinAckFrameSize(
      QuicTransportVersion version,
      const QuicAckFrame& ack_frame,
      uint32_t local_ack_delay_exponent,
      QuicPacketNumberLength largest_observed_length);
  // Size in bytes of a stop waiting frame.
  static size_t GetStopWaitingFrameSize(
      QuicPacketNumberLength packet_number_length);
  // Size in bytes of all reset stream frame fields.
  static size_t GetRstStreamFrameSize(QuicTransportVersion version,
                                      const QuicRstStreamFrame& frame);
  // Size in bytes of all connection close frame fields, including the error
  // details.
  static size_t GetConnectionCloseFrameSize(
      QuicTransportVersion version,
      const QuicConnectionCloseFrame& frame);
  // Size in bytes of all GoAway frame fields without the reason phrase.
  static size_t GetMinGoAwayFrameSize();
  // Size in bytes of all WindowUpdate frame fields.
  // For version 99, determines whether a MAX DATA or MAX STREAM DATA frame will
  // be generated and calculates the appropriate size.
  static size_t GetWindowUpdateFrameSize(QuicTransportVersion version,
                                         const QuicWindowUpdateFrame& frame);
  // Size in bytes of all MaxStreams frame fields.
  static size_t GetMaxStreamsFrameSize(QuicTransportVersion version,
                                       const QuicMaxStreamsFrame& frame);
  // Size in bytes of all StreamsBlocked frame fields.
  static size_t GetStreamsBlockedFrameSize(
      QuicTransportVersion version,
      const QuicStreamsBlockedFrame& frame);
  // Size in bytes of all Blocked frame fields.
  static size_t GetBlockedFrameSize(QuicTransportVersion version,
                                    const QuicBlockedFrame& frame);
  // Size in bytes of PathChallenge frame.
  static size_t GetPathChallengeFrameSize(const QuicPathChallengeFrame& frame);
  // Size in bytes of PathResponse frame.
  static size_t GetPathResponseFrameSize(const QuicPathResponseFrame& frame);
  // Size in bytes required to serialize the stream id.
  static size_t GetStreamIdSize(QuicStreamId stream_id);
  // Size in bytes required to serialize the stream offset.
  static size_t GetStreamOffsetSize(QuicStreamOffset offset);
  // Size in bytes for a serialized new connection id frame
  static size_t GetNewConnectionIdFrameSize(
      const QuicNewConnectionIdFrame& frame);

  // Size in bytes for a serialized retire connection id frame
  static size_t GetRetireConnectionIdFrameSize(
      const QuicRetireConnectionIdFrame& frame);

  // Size in bytes for a serialized new token frame
  static size_t GetNewTokenFrameSize(const QuicNewTokenFrame& frame);

  // Size in bytes required for a serialized stop sending frame.
  static size_t GetStopSendingFrameSize(const QuicStopSendingFrame& frame);

  // Size in bytes required for a serialized retransmittable control |frame|.
  static size_t GetRetransmittableControlFrameSize(QuicTransportVersion version,
                                                   const QuicFrame& frame);

  // Returns the number of bytes added to the packet for the specified frame,
  // and 0 if the frame doesn't fit.  Includes the header size for the first
  // frame.
  size_t GetSerializedFrameLength(const QuicFrame& frame,
                                  size_t free_bytes,
                                  bool first_frame_in_packet,
                                  bool last_frame_in_packet,
                                  QuicPacketNumberLength packet_number_length);

  // Returns the associated data from the encrypted packet |encrypted| as a
  // stringpiece.
  static quiche::QuicheStringPiece GetAssociatedDataFromEncryptedPacket(
      QuicTransportVersion version,
      const QuicEncryptedPacket& encrypted,
      QuicConnectionIdLength destination_connection_id_length,
      QuicConnectionIdLength source_connection_id_length,
      bool includes_version,
      bool includes_diversification_nonce,
      QuicPacketNumberLength packet_number_length,
      QuicVariableLengthIntegerLength retry_token_length_length,
      uint64_t retry_token_length,
      QuicVariableLengthIntegerLength length_length);

  // Parses the unencryoted fields in a QUIC header using |reader| as input,
  // stores the result in the other parameters.
  // |expected_destination_connection_id_length| is only used for short headers.
  static QuicErrorCode ParsePublicHeader(
      QuicDataReader* reader,
      uint8_t expected_destination_connection_id_length,
      bool ietf_format,
      uint8_t* first_byte,
      PacketHeaderFormat* format,
      bool* version_present,
      bool* has_length_prefix,
      QuicVersionLabel* version_label,
      ParsedQuicVersion* parsed_version,
      QuicConnectionId* destination_connection_id,
      QuicConnectionId* source_connection_id,
      QuicLongHeaderType* long_packet_type,
      QuicVariableLengthIntegerLength* retry_token_length_length,
      quiche::QuicheStringPiece* retry_token,
      std::string* detailed_error);

  // Parses the unencryoted fields in |packet| and stores them in the other
  // parameters. This can only be called on the server.
  // |expected_destination_connection_id_length| is only used for short headers.
  static QuicErrorCode ParsePublicHeaderDispatcher(
      const QuicEncryptedPacket& packet,
      uint8_t expected_destination_connection_id_length,
      PacketHeaderFormat* format,
      QuicLongHeaderType* long_packet_type,
      bool* version_present,
      bool* has_length_prefix,
      QuicVersionLabel* version_label,
      ParsedQuicVersion* parsed_version,
      QuicConnectionId* destination_connection_id,
      QuicConnectionId* source_connection_id,
      bool* retry_token_present,
      quiche::QuicheStringPiece* retry_token,
      std::string* detailed_error);

  // Serializes a packet containing |frames| into |buffer|.
  // Returns the length of the packet, which must not be longer than
  // |packet_length|.  Returns 0 if it fails to serialize.
  size_t BuildDataPacket(const QuicPacketHeader& header,
                         const QuicFrames& frames,
                         char* buffer,
                         size_t packet_length,
                         EncryptionLevel level);

  // Returns a new public reset packet.
  static std::unique_ptr<QuicEncryptedPacket> BuildPublicResetPacket(
      const QuicPublicResetPacket& packet);

  // Returns a new IETF stateless reset packet.
  static std::unique_ptr<QuicEncryptedPacket> BuildIetfStatelessResetPacket(
      QuicConnectionId connection_id,
      QuicUint128 stateless_reset_token);

  // Returns a new version negotiation packet.
  static std::unique_ptr<QuicEncryptedPacket> BuildVersionNegotiationPacket(
      QuicConnectionId server_connection_id,
      QuicConnectionId client_connection_id,
      bool ietf_quic,
      bool use_length_prefix,
      const ParsedQuicVersionVector& versions);

  // Returns a new IETF version negotiation packet.
  static std::unique_ptr<QuicEncryptedPacket> BuildIetfVersionNegotiationPacket(
      bool use_length_prefix,
      QuicConnectionId server_connection_id,
      QuicConnectionId client_connection_id,
      const ParsedQuicVersionVector& versions);

  // If header.version_flag is set, the version in the
  // packet will be set -- but it will be set from version_ not
  // header.versions.
  bool AppendPacketHeader(const QuicPacketHeader& header,
                          QuicDataWriter* writer,
                          size_t* length_field_offset);
  bool AppendIetfHeaderTypeByte(const QuicPacketHeader& header,
                                QuicDataWriter* writer);
  bool AppendIetfPacketHeader(const QuicPacketHeader& header,
                              QuicDataWriter* writer,
                              size_t* length_field_offset);
  bool WriteIetfLongHeaderLength(const QuicPacketHeader& header,
                                 QuicDataWriter* writer,
                                 size_t length_field_offset,
                                 EncryptionLevel level);
  bool AppendTypeByte(const QuicFrame& frame,
                      bool last_frame_in_packet,
                      QuicDataWriter* writer);
  bool AppendIetfTypeByte(const QuicFrame& frame,
                          bool last_frame_in_packet,
                          QuicDataWriter* writer);
  size_t AppendIetfFrames(const QuicFrames& frames, QuicDataWriter* writer);
  bool AppendStreamFrame(const QuicStreamFrame& frame,
                         bool last_frame_in_packet,
                         QuicDataWriter* writer);
  bool AppendCryptoFrame(const QuicCryptoFrame& frame, QuicDataWriter* writer);

  // SetDecrypter sets the primary decrypter, replacing any that already exists.
  // If an alternative decrypter is in place then the function DCHECKs. This is
  // intended for cases where one knows that future packets will be using the
  // new decrypter and the previous decrypter is now obsolete. |level| indicates
  // the encryption level of the new decrypter.
  void SetDecrypter(EncryptionLevel level,
                    std::unique_ptr<QuicDecrypter> decrypter);

  // SetAlternativeDecrypter sets a decrypter that may be used to decrypt
  // future packets. |level| indicates the encryption level of the decrypter. If
  // |latch_once_used| is true, then the first time that the decrypter is
  // successful it will replace the primary decrypter.  Otherwise both
  // decrypters will remain active and the primary decrypter will be the one
  // last used.
  void SetAlternativeDecrypter(EncryptionLevel level,
                               std::unique_ptr<QuicDecrypter> decrypter,
                               bool latch_once_used);

  void InstallDecrypter(EncryptionLevel level,
                        std::unique_ptr<QuicDecrypter> decrypter);
  void RemoveDecrypter(EncryptionLevel level);

  const QuicDecrypter* GetDecrypter(EncryptionLevel level) const;
  const QuicDecrypter* decrypter() const;
  const QuicDecrypter* alternative_decrypter() const;

  // Changes the encrypter used for level |level| to |encrypter|.
  void SetEncrypter(EncryptionLevel level,
                    std::unique_ptr<QuicEncrypter> encrypter);

  // Called to remove encrypter of encryption |level|.
  void RemoveEncrypter(EncryptionLevel level);

  // Sets the encrypter and decrypter for the ENCRYPTION_INITIAL level.
  void SetInitialObfuscators(QuicConnectionId connection_id);

  // Encrypts a payload in |buffer|.  |ad_len| is the length of the associated
  // data. |total_len| is the length of the associated data plus plaintext.
  // |buffer_len| is the full length of the allocated buffer.
  size_t EncryptInPlace(EncryptionLevel level,
                        QuicPacketNumber packet_number,
                        size_t ad_len,
                        size_t total_len,
                        size_t buffer_len,
                        char* buffer);

  // Returns the length of the data encrypted into |buffer| if |buffer_len| is
  // long enough, and otherwise 0.
  size_t EncryptPayload(EncryptionLevel level,
                        QuicPacketNumber packet_number,
                        const QuicPacket& packet,
                        char* buffer,
                        size_t buffer_len);

  // Returns the length of the ciphertext that would be generated by encrypting
  // to plaintext of size |plaintext_size| at the given level.
  size_t GetCiphertextSize(EncryptionLevel level, size_t plaintext_size) const;

  // Returns the maximum length of plaintext that can be encrypted
  // to ciphertext no larger than |ciphertext_size|.
  size_t GetMaxPlaintextSize(size_t ciphertext_size);

  const std::string& detailed_error() { return detailed_error_; }

  // The minimum packet number length required to represent |packet_number|.
  static QuicPacketNumberLength GetMinPacketNumberLength(
      QuicPacketNumber packet_number);

  void SetSupportedVersions(const ParsedQuicVersionVector& versions) {
    supported_versions_ = versions;
    version_ = versions[0];
  }

  // Tell framer to infer packet header type from version_.
  void InferPacketHeaderTypeFromVersion();

  // Returns true if |header| is considered as an stateless reset packet.
  bool IsIetfStatelessResetPacket(const QuicPacketHeader& header) const;

  // Returns true if encrypter of |level| is available.
  bool HasEncrypterOfEncryptionLevel(EncryptionLevel level) const;

  void set_validate_flags(bool value) { validate_flags_ = value; }

  Perspective perspective() const { return perspective_; }

  void set_data_producer(QuicStreamFrameDataProducer* data_producer) {
    data_producer_ = data_producer;
  }

  QuicTime creation_time() const { return creation_time_; }

  QuicPacketNumber first_sending_packet_number() const {
    return first_sending_packet_number_;
  }

  uint64_t current_received_frame_type() const {
    return current_received_frame_type_;
  }

  // The connection ID length the framer expects on incoming IETF short headers
  // on the server.
  uint8_t GetExpectedServerConnectionIdLength() {
    return expected_server_connection_id_length_;
  }

  // Change the expected destination connection ID length for short headers on
  // the client.
  void SetExpectedClientConnectionIdLength(
      uint8_t expected_client_connection_id_length) {
    expected_client_connection_id_length_ =
        expected_client_connection_id_length;
  }

  void EnableMultiplePacketNumberSpacesSupport();

  // Writes an array of bytes that, if sent as a UDP datagram, will trigger
  // IETF QUIC Version Negotiation on servers. The bytes will be written to
  // |packet_bytes|, which must point to |packet_length| bytes of memory.
  // |packet_length| must be in the range [1200, 65535].
  // |destination_connection_id_bytes| will be sent as the destination
  // connection ID, and must point to |destination_connection_id_length| bytes
  // of memory. |destination_connection_id_length| must be in the range [8,18].
  // When targeting Google servers, it is recommended to use a
  // |destination_connection_id_length| of 8.
  static bool WriteClientVersionNegotiationProbePacket(
      char* packet_bytes,
      QuicByteCount packet_length,
      const char* destination_connection_id_bytes,
      uint8_t destination_connection_id_length);

  // Parses a packet which a QUIC server sent in response to a packet sent by
  // WriteClientVersionNegotiationProbePacket. |packet_bytes| must point to
  // |packet_length| bytes in memory which represent the response.
  // |packet_length| must be greater or equal to 6. This method will fill in
  // |source_connection_id_bytes| which must point to at least 18 bytes in
  // memory. |source_connection_id_length_out| will contain the length of the
  // received source connection ID, which on success will match the contents of
  // the destination connection ID passed in to
  // WriteClientVersionNegotiationProbePacket. In the case of a failure,
  // |detailed_error| will be filled in with an explanation of what failed.
  static bool ParseServerVersionNegotiationProbeResponse(
      const char* packet_bytes,
      QuicByteCount packet_length,
      char* source_connection_id_bytes,
      uint8_t* source_connection_id_length_out,
      std::string* detailed_error);

  void set_local_ack_delay_exponent(uint32_t exponent) {
    local_ack_delay_exponent_ = exponent;
  }
  uint32_t local_ack_delay_exponent() const {
    return local_ack_delay_exponent_;
  }

  void set_peer_ack_delay_exponent(uint32_t exponent) {
    peer_ack_delay_exponent_ = exponent;
  }
  uint32_t peer_ack_delay_exponent() const { return peer_ack_delay_exponent_; }

 private:
  friend class test::QuicFramerPeer;

  typedef std::map<QuicPacketNumber, uint8_t> NackRangeMap;

  struct QUIC_EXPORT_PRIVATE AckFrameInfo {
    AckFrameInfo();
    AckFrameInfo(const AckFrameInfo& other);
    ~AckFrameInfo();

    // The maximum ack block length.
    QuicPacketCount max_block_length;
    // Length of first ack block.
    QuicPacketCount first_block_length;
    // Number of ACK blocks needed for the ACK frame.
    size_t num_ack_blocks;
  };

  // Applies header protection to an IETF QUIC packet header in |buffer| using
  // the encrypter for level |level|. The buffer has |buffer_len| bytes of data,
  // with the first protected packet bytes starting at |ad_len|.
  bool ApplyHeaderProtection(EncryptionLevel level,
                             char* buffer,
                             size_t buffer_len,
                             size_t ad_len);

  // Removes header protection from an IETF QUIC packet header.
  //
  // The packet number from the header is read from |reader|, where the packet
  // number is the next contents in |reader|. |reader| is only advanced by the
  // length of the packet number, but it is also used to peek the sample needed
  // for removing header protection.
  //
  // Properties needed for removing header protection are read from |header|.
  // The packet number length and type byte are written to |header|.
  //
  // The packet number, after removing header protection and decoding it, is
  // written to |full_packet_number|. Finally, the header, with header
  // protection removed, is written to |associated_data| to be used in packet
  // decryption. |packet| is used in computing the asociated data.
  bool RemoveHeaderProtection(QuicDataReader* reader,
                              const QuicEncryptedPacket& packet,
                              QuicPacketHeader* header,
                              uint64_t* full_packet_number,
                              std::vector<char>* associated_data);

  bool ProcessDataPacket(QuicDataReader* reader,
                         QuicPacketHeader* header,
                         const QuicEncryptedPacket& packet,
                         char* decrypted_buffer,
                         size_t buffer_length);

  bool ProcessIetfDataPacket(QuicDataReader* encrypted_reader,
                             QuicPacketHeader* header,
                             const QuicEncryptedPacket& packet,
                             char* decrypted_buffer,
                             size_t buffer_length);

  bool ProcessPublicResetPacket(QuicDataReader* reader,
                                const QuicPacketHeader& header);

  bool ProcessVersionNegotiationPacket(QuicDataReader* reader,
                                       const QuicPacketHeader& header);

  bool ProcessRetryPacket(QuicDataReader* reader,
                          const QuicPacketHeader& header);

  void MaybeProcessCoalescedPacket(const QuicDataReader& encrypted_reader,
                                   uint64_t remaining_bytes_length,
                                   const QuicPacketHeader& header);

  bool MaybeProcessIetfLength(QuicDataReader* encrypted_reader,
                              QuicPacketHeader* header);

  bool ProcessPublicHeader(QuicDataReader* reader,
                           bool packet_has_ietf_packet_header,
                           QuicPacketHeader* header);

  // Processes the unauthenticated portion of the header into |header| from
  // the current QuicDataReader.  Returns true on success, false on failure.
  bool ProcessUnauthenticatedHeader(QuicDataReader* encrypted_reader,
                                    QuicPacketHeader* header);

  // Processes the version label in the packet header.
  static bool ProcessVersionLabel(QuicDataReader* reader,
                                  QuicVersionLabel* version_label);

  // Validates and updates |destination_connection_id_length| and
  // |source_connection_id_length|. When
  // |should_update_expected_server_connection_id_length| is true, length
  // validation is disabled and |expected_server_connection_id_length| is set
  // to the appropriate length.
  // TODO(b/133873272) refactor this method.
  static bool ProcessAndValidateIetfConnectionIdLength(
      QuicDataReader* reader,
      ParsedQuicVersion version,
      Perspective perspective,
      bool should_update_expected_server_connection_id_length,
      uint8_t* expected_server_connection_id_length,
      uint8_t* destination_connection_id_length,
      uint8_t* source_connection_id_length,
      std::string* detailed_error);

  bool ProcessIetfHeaderTypeByte(QuicDataReader* reader,
                                 QuicPacketHeader* header);
  bool ProcessIetfPacketHeader(QuicDataReader* reader,
                               QuicPacketHeader* header);

  // First processes possibly truncated packet number. Calculates the full
  // packet number from the truncated one and the last seen packet number, and
  // stores it to |packet_number|.
  bool ProcessAndCalculatePacketNumber(
      QuicDataReader* reader,
      QuicPacketNumberLength packet_number_length,
      QuicPacketNumber base_packet_number,
      uint64_t* packet_number);
  bool ProcessFrameData(QuicDataReader* reader, const QuicPacketHeader& header);
  bool ProcessIetfFrameData(QuicDataReader* reader,
                            const QuicPacketHeader& header);
  bool ProcessStreamFrame(QuicDataReader* reader,
                          uint8_t frame_type,
                          QuicStreamFrame* frame);
  bool ProcessAckFrame(QuicDataReader* reader, uint8_t frame_type);
  bool ProcessTimestampsInAckFrame(uint8_t num_received_packets,
                                   QuicPacketNumber largest_acked,
                                   QuicDataReader* reader);
  bool ProcessIetfAckFrame(QuicDataReader* reader,
                           uint64_t frame_type,
                           QuicAckFrame* ack_frame);
  bool ProcessStopWaitingFrame(QuicDataReader* reader,
                               const QuicPacketHeader& header,
                               QuicStopWaitingFrame* stop_waiting);
  bool ProcessRstStreamFrame(QuicDataReader* reader, QuicRstStreamFrame* frame);
  bool ProcessConnectionCloseFrame(QuicDataReader* reader,
                                   QuicConnectionCloseFrame* frame);
  bool ProcessGoAwayFrame(QuicDataReader* reader, QuicGoAwayFrame* frame);
  bool ProcessWindowUpdateFrame(QuicDataReader* reader,
                                QuicWindowUpdateFrame* frame);
  bool ProcessBlockedFrame(QuicDataReader* reader, QuicBlockedFrame* frame);
  void ProcessPaddingFrame(QuicDataReader* reader, QuicPaddingFrame* frame);
  bool ProcessMessageFrame(QuicDataReader* reader,
                           bool no_message_length,
                           QuicMessageFrame* frame);

  bool DecryptPayload(quiche::QuicheStringPiece encrypted,
                      quiche::QuicheStringPiece associated_data,
                      const QuicPacketHeader& header,
                      char* decrypted_buffer,
                      size_t buffer_length,
                      size_t* decrypted_length,
                      EncryptionLevel* decrypted_level);

  // Returns the full packet number from the truncated
  // wire format version and the last seen packet number.
  uint64_t CalculatePacketNumberFromWire(
      QuicPacketNumberLength packet_number_length,
      QuicPacketNumber base_packet_number,
      uint64_t packet_number) const;

  // Returns the QuicTime::Delta corresponding to the time from when the framer
  // was created.
  const QuicTime::Delta CalculateTimestampFromWire(uint32_t time_delta_us);

  // Computes the wire size in bytes of time stamps in |ack|.
  size_t GetAckFrameTimeStampSize(const QuicAckFrame& ack);

  // Computes the wire size in bytes of the |ack| frame.
  size_t GetAckFrameSize(const QuicAckFrame& ack,
                         QuicPacketNumberLength packet_number_length);
  // Computes the wire-size, in bytes, of the |frame| ack frame, for IETF Quic.
  size_t GetIetfAckFrameSize(const QuicAckFrame& frame);

  // Computes the wire size in bytes of the |ack| frame.
  size_t GetAckFrameSize(const QuicAckFrame& ack);

  // Computes the wire size in bytes of the payload of |frame|.
  size_t ComputeFrameLength(const QuicFrame& frame,
                            bool last_frame_in_packet,
                            QuicPacketNumberLength packet_number_length);

  static bool AppendPacketNumber(QuicPacketNumberLength packet_number_length,
                                 QuicPacketNumber packet_number,
                                 QuicDataWriter* writer);
  static bool AppendStreamId(size_t stream_id_length,
                             QuicStreamId stream_id,
                             QuicDataWriter* writer);
  static bool AppendStreamOffset(size_t offset_length,
                                 QuicStreamOffset offset,
                                 QuicDataWriter* writer);

  // Appends a single ACK block to |writer| and returns true if the block was
  // successfully appended.
  static bool AppendAckBlock(uint8_t gap,
                             QuicPacketNumberLength length_length,
                             uint64_t length,
                             QuicDataWriter* writer);

  static uint8_t GetPacketNumberFlags(
      QuicPacketNumberLength packet_number_length);

  static AckFrameInfo GetAckFrameInfo(const QuicAckFrame& frame);

  static QuicErrorCode ParsePublicHeaderGoogleQuic(
      QuicDataReader* reader,
      uint8_t* first_byte,
      PacketHeaderFormat* format,
      bool* version_present,
      QuicVersionLabel* version_label,
      ParsedQuicVersion* parsed_version,
      QuicConnectionId* destination_connection_id,
      std::string* detailed_error);

  bool ValidateReceivedConnectionIds(const QuicPacketHeader& header);

  // The Append* methods attempt to write the provided header or frame using the
  // |writer|, and return true if successful.

  bool AppendAckFrameAndTypeByte(const QuicAckFrame& frame,
                                 QuicDataWriter* builder);
  bool AppendTimestampsToAckFrame(const QuicAckFrame& frame,
                                  QuicDataWriter* writer);

  // Append IETF format ACK frame.
  //
  // AppendIetfAckFrameAndTypeByte adds the IETF type byte and the body
  // of the frame.
  bool AppendIetfAckFrameAndTypeByte(const QuicAckFrame& frame,
                                     QuicDataWriter* writer);

  bool AppendStopWaitingFrame(const QuicPacketHeader& header,
                              const QuicStopWaitingFrame& frame,
                              QuicDataWriter* builder);
  bool AppendRstStreamFrame(const QuicRstStreamFrame& frame,
                            QuicDataWriter* builder);
  bool AppendConnectionCloseFrame(const QuicConnectionCloseFrame& frame,
                                  QuicDataWriter* builder);
  bool AppendGoAwayFrame(const QuicGoAwayFrame& frame, QuicDataWriter* writer);
  bool AppendWindowUpdateFrame(const QuicWindowUpdateFrame& frame,
                               QuicDataWriter* writer);
  bool AppendBlockedFrame(const QuicBlockedFrame& frame,
                          QuicDataWriter* writer);
  bool AppendPaddingFrame(const QuicPaddingFrame& frame,
                          QuicDataWriter* writer);
  bool AppendMessageFrameAndTypeByte(const QuicMessageFrame& frame,
                                     bool last_frame_in_packet,
                                     QuicDataWriter* writer);

  // IETF frame processing methods.
  bool ProcessIetfStreamFrame(QuicDataReader* reader,
                              uint8_t frame_type,
                              QuicStreamFrame* frame);
  bool ProcessIetfConnectionCloseFrame(QuicDataReader* reader,
                                       QuicConnectionCloseType type,
                                       QuicConnectionCloseFrame* frame);
  bool ProcessPathChallengeFrame(QuicDataReader* reader,
                                 QuicPathChallengeFrame* frame);
  bool ProcessPathResponseFrame(QuicDataReader* reader,
                                QuicPathResponseFrame* frame);
  bool ProcessIetfResetStreamFrame(QuicDataReader* reader,
                                   QuicRstStreamFrame* frame);
  bool ProcessStopSendingFrame(QuicDataReader* reader,
                               QuicStopSendingFrame* stop_sending_frame);
  bool ProcessCryptoFrame(QuicDataReader* reader,
                          EncryptionLevel encryption_level,
                          QuicCryptoFrame* frame);

  // IETF frame appending methods.  All methods append the type byte as well.
  bool AppendIetfStreamFrame(const QuicStreamFrame& frame,
                             bool last_frame_in_packet,
                             QuicDataWriter* writer);
  bool AppendIetfConnectionCloseFrame(const QuicConnectionCloseFrame& frame,
                                      QuicDataWriter* writer);
  bool AppendPathChallengeFrame(const QuicPathChallengeFrame& frame,
                                QuicDataWriter* writer);
  bool AppendPathResponseFrame(const QuicPathResponseFrame& frame,
                               QuicDataWriter* writer);
  bool AppendIetfResetStreamFrame(const QuicRstStreamFrame& frame,
                                  QuicDataWriter* writer);
  bool AppendStopSendingFrame(const QuicStopSendingFrame& stop_sending_frame,
                              QuicDataWriter* writer);

  // Append/consume IETF-Format MAX_DATA and MAX_STREAM_DATA frames
  bool AppendMaxDataFrame(const QuicWindowUpdateFrame& frame,
                          QuicDataWriter* writer);
  bool AppendMaxStreamDataFrame(const QuicWindowUpdateFrame& frame,
                                QuicDataWriter* writer);
  bool ProcessMaxDataFrame(QuicDataReader* reader,
                           QuicWindowUpdateFrame* frame);
  bool ProcessMaxStreamDataFrame(QuicDataReader* reader,
                                 QuicWindowUpdateFrame* frame);

  bool AppendMaxStreamsFrame(const QuicMaxStreamsFrame& frame,
                             QuicDataWriter* writer);
  bool ProcessMaxStreamsFrame(QuicDataReader* reader,
                              QuicMaxStreamsFrame* frame,
                              uint64_t frame_type);

  bool AppendDataBlockedFrame(const QuicBlockedFrame& frame,
                              QuicDataWriter* writer);
  bool ProcessDataBlockedFrame(QuicDataReader* reader, QuicBlockedFrame* frame);

  bool AppendStreamDataBlockedFrame(const QuicBlockedFrame& frame,
                                    QuicDataWriter* writer);
  bool ProcessStreamDataBlockedFrame(QuicDataReader* reader,
                                     QuicBlockedFrame* frame);

  bool AppendStreamsBlockedFrame(const QuicStreamsBlockedFrame& frame,
                                 QuicDataWriter* writer);
  bool ProcessStreamsBlockedFrame(QuicDataReader* reader,
                                  QuicStreamsBlockedFrame* frame,
                                  uint64_t frame_type);

  bool AppendNewConnectionIdFrame(const QuicNewConnectionIdFrame& frame,
                                  QuicDataWriter* writer);
  bool ProcessNewConnectionIdFrame(QuicDataReader* reader,
                                   QuicNewConnectionIdFrame* frame);
  bool AppendRetireConnectionIdFrame(const QuicRetireConnectionIdFrame& frame,
                                     QuicDataWriter* writer);
  bool ProcessRetireConnectionIdFrame(QuicDataReader* reader,
                                      QuicRetireConnectionIdFrame* frame);

  bool AppendNewTokenFrame(const QuicNewTokenFrame& frame,
                           QuicDataWriter* writer);
  bool ProcessNewTokenFrame(QuicDataReader* reader, QuicNewTokenFrame* frame);

  bool RaiseError(QuicErrorCode error);

  // Returns true if |header| indicates a version negotiation packet.
  bool IsVersionNegotiation(const QuicPacketHeader& header,
                            bool packet_has_ietf_packet_header) const;

  // Calculates and returns type byte of stream frame.
  uint8_t GetStreamFrameTypeByte(const QuicStreamFrame& frame,
                                 bool last_frame_in_packet) const;
  uint8_t GetIetfStreamFrameTypeByte(const QuicStreamFrame& frame,
                                     bool last_frame_in_packet) const;

  void set_error(QuicErrorCode error) { error_ = error; }

  void set_detailed_error(const char* error) { detailed_error_ = error; }
  void set_detailed_error(std::string error) { detailed_error_ = error; }

  // Returns false if the reading fails.
  bool ReadUint32FromVarint62(QuicDataReader* reader,
                              QuicIetfFrameType type,
                              QuicStreamId* id);

  std::string detailed_error_;
  QuicFramerVisitorInterface* visitor_;
  QuicErrorCode error_;
  // Updated by ProcessPacketHeader when it succeeds decrypting a larger packet.
  QuicPacketNumber largest_packet_number_;
  // Largest successfully decrypted packet number per packet number space. Only
  // used when supports_multiple_packet_number_spaces_ is true.
  QuicPacketNumber largest_decrypted_packet_numbers_[NUM_PACKET_NUMBER_SPACES];
  // Last server connection ID seen on the wire.
  QuicConnectionId last_serialized_server_connection_id_;
  // Last client connection ID seen on the wire.
  QuicConnectionId last_serialized_client_connection_id_;
  // Version of the protocol being used.
  ParsedQuicVersion version_;
  // This vector contains QUIC versions which we currently support.
  // This should be ordered such that the highest supported version is the first
  // element, with subsequent elements in descending order (versions can be
  // skipped as necessary).
  ParsedQuicVersionVector supported_versions_;
  // Decrypters used to decrypt packets during parsing.
  std::unique_ptr<QuicDecrypter> decrypter_[NUM_ENCRYPTION_LEVELS];
  // The encryption level of the primary decrypter to use in |decrypter_|.
  EncryptionLevel decrypter_level_;
  // The encryption level of the alternative decrypter to use in |decrypter_|.
  // When set to NUM_ENCRYPTION_LEVELS, indicates that there is no alternative
  // decrypter.
  EncryptionLevel alternative_decrypter_level_;
  // |alternative_decrypter_latch_| is true if, when the decrypter at
  // |alternative_decrypter_level_| successfully decrypts a packet, we should
  // install it as the only decrypter.
  bool alternative_decrypter_latch_;
  // Encrypters used to encrypt packets via EncryptPayload().
  std::unique_ptr<QuicEncrypter> encrypter_[NUM_ENCRYPTION_LEVELS];
  // Tracks if the framer is being used by the entity that received the
  // connection or the entity that initiated it.
  Perspective perspective_;
  // If false, skip validation that the public flags are set to legal values.
  bool validate_flags_;
  // The diversification nonce from the last received packet.
  DiversificationNonce last_nonce_;
  // If true, send and process timestamps in the ACK frame.
  bool process_timestamps_;
  // The creation time of the connection, used to calculate timestamps.
  QuicTime creation_time_;
  // The last timestamp received if process_timestamps_ is true.
  QuicTime::Delta last_timestamp_;

  // If this is a framer of a connection, this is the packet number of first
  // sending packet. If this is a framer of a framer of dispatcher, this is the
  // packet number of sent packets (for those which have packet number).
  const QuicPacketNumber first_sending_packet_number_;

  // If not null, framer asks data_producer_ to write stream frame data. Not
  // owned. TODO(fayang): Consider add data producer to framer's constructor.
  QuicStreamFrameDataProducer* data_producer_;

  // If true, framer infers packet header type (IETF/GQUIC) from version_.
  // Otherwise, framer infers packet header type from first byte of a received
  // packet.
  bool infer_packet_header_type_from_version_;

  // IETF short headers contain a destination connection ID but do not
  // encode its length. These variables contains the length we expect to read.
  // This is also used to validate the long header destination connection ID
  // lengths in older versions of QUIC.
  uint8_t expected_server_connection_id_length_;
  uint8_t expected_client_connection_id_length_;

  // Indicates whether this framer supports multiple packet number spaces.
  bool supports_multiple_packet_number_spaces_;

  // The length in bytes of the last packet number written to an IETF-framed
  // packet.
  size_t last_written_packet_number_length_;

  // The amount to shift the ack timestamp in ACK frames. The default is 3.
  // Local_ is the amount this node shifts timestamps in ACK frames it
  // generates. it is sent to the peer in a transport parameter negotiation.
  // Peer_ is the amount the peer shifts timestamps when it sends ACK frames to
  // this node. This node "unshifts" by this amount. The value is received from
  // the peer in the transport parameter negotiation. IETF QUIC only.
  uint32_t peer_ack_delay_exponent_;
  uint32_t local_ack_delay_exponent_;

  // The type of received IETF frame currently being processed.  0 when not
  // processing a frame or when processing Google QUIC frames.  Used to populate
  // the Transport Connection Close when there is an error during frame
  // processing.
  uint64_t current_received_frame_type_;
};

// Look for and parse the error code from the "<quic_error_code>:" text that
// may be present at the start of the CONNECTION_CLOSE error details string.
// This text, inserted by the peer if it's using Google's QUIC implementation,
// contains additional error information that narrows down the exact error. The
// extracted error code and (possibly updated) error_details string are returned
// in |*frame|. If an error code is not found in the error details then the
// extracted_error_code is set to QuicErrorCode::QUIC_IETF_GQUIC_ERROR_MISSING.
// If there is an error code in the string then it is removed from the string.
QUIC_EXPORT_PRIVATE void MaybeExtractQuicErrorCode(
    QuicConnectionCloseFrame* frame);

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_FRAMER_H_
