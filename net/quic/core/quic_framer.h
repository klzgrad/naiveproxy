// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_QUIC_CORE_QUIC_FRAMER_H_
#define NET_QUIC_CORE_QUIC_FRAMER_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "base/macros.h"
#include "net/quic/core/quic_iovector.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/platform/api/quic_endian.h"
#include "net/quic/platform/api/quic_export.h"
#include "net/quic/platform/api/quic_string_piece.h"

namespace net {

namespace test {
class QuicFramerPeer;
}  // namespace test

class QuicDataReader;
class QuicDataWriter;
class QuicDecrypter;
class QuicEncrypter;
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

  // Called only when |perspective_| is IS_SERVER and the the framer gets a
  // packet with version flag true and the version on the packet doesn't match
  // |quic_version_|. The visitor should return true after it updates the
  // version of the |framer_| to |received_version| or false to stop processing
  // this packet.
  virtual bool OnProtocolVersionMismatch(
      QuicTransportVersion received_version) = 0;

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

  // Called when the public header has been parsed, but has not been
  // authenticated. If it returns false, framing for this packet will cease.
  virtual bool OnUnauthenticatedPublicHeader(
      const QuicPacketPublicHeader& header) = 0;

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

  // Called when a StreamFrame has been parsed.
  virtual bool OnStreamFrame(const QuicStreamFrame& frame) = 0;

  // Called when a AckFrame has been parsed.  If OnAckFrame returns false,
  // the framer will stop parsing the current packet.
  virtual bool OnAckFrame(const QuicAckFrame& frame) = 0;

  // Called when a StopWaitingFrame has been parsed.
  virtual bool OnStopWaitingFrame(const QuicStopWaitingFrame& frame) = 0;

  // Called when a QuicPaddingFrame has been parsed.
  virtual bool OnPaddingFrame(const QuicPaddingFrame& frame) = 0;

  // Called when a PingFrame has been parsed.
  virtual bool OnPingFrame(const QuicPingFrame& frame) = 0;

  // Called when a RstStreamFrame has been parsed.
  virtual bool OnRstStreamFrame(const QuicRstStreamFrame& frame) = 0;

  // Called when a ConnectionCloseFrame has been parsed.
  virtual bool OnConnectionCloseFrame(
      const QuicConnectionCloseFrame& frame) = 0;

  // Called when a GoAwayFrame has been parsed.
  virtual bool OnGoAwayFrame(const QuicGoAwayFrame& frame) = 0;

  // Called when a WindowUpdateFrame has been parsed.
  virtual bool OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame) = 0;

  // Called when a BlockedFrame has been parsed.
  virtual bool OnBlockedFrame(const QuicBlockedFrame& frame) = 0;

  // Called when a packet has been completely processed.
  virtual void OnPacketComplete() = 0;
};

// Class for parsing and constructing QUIC packets.  It has a
// QuicFramerVisitorInterface that is called when packets are parsed.
class QUIC_EXPORT_PRIVATE QuicFramer {
 public:
  // Constructs a new framer that installs a kNULL QuicEncrypter and
  // QuicDecrypter for level ENCRYPTION_NONE. |supported_versions| specifies the
  // list of supported QUIC versions. |quic_version_| is set to the maximum
  // version in |supported_versions|.
  QuicFramer(const QuicTransportVersionVector& supported_versions,
             QuicTime creation_time,
             Perspective perspective);

  virtual ~QuicFramer();

  // Returns true if |version| is a supported protocol version.
  bool IsSupportedVersion(const QuicTransportVersion version) const;

  // Set callbacks to be called from the framer.  A visitor must be set, or
  // else the framer will likely crash.  It is acceptable for the visitor
  // to do nothing.  If this is called multiple times, only the last visitor
  // will be used.
  void set_visitor(QuicFramerVisitorInterface* visitor) { visitor_ = visitor; }

  const QuicTransportVersionVector& supported_versions() const {
    return supported_versions_;
  }

  QuicTransportVersion transport_version() const { return transport_version_; }

  void set_version(const QuicTransportVersion version);

  // Does not DCHECK for supported version. Used by tests to set unsupported
  // version to trigger version negotiation.
  void set_version_for_tests(const QuicTransportVersion version) {
    transport_version_ = version;
  }

  QuicErrorCode error() const { return error_; }

  // Pass a UDP packet into the framer for parsing.
  // Return true if the packet was processed succesfully. |packet| must be a
  // single, complete UDP packet (not a frame of a packet).  This packet
  // might be null padded past the end of the payload, which will be correctly
  // ignored.
  bool ProcessPacket(const QuicEncryptedPacket& packet);

  // Largest size in bytes of all stream frame fields without the payload.
  static size_t GetMinStreamFrameSize(QuicTransportVersion version,
                                      QuicStreamId stream_id,
                                      QuicStreamOffset offset,
                                      bool last_frame_in_packet);
  // Size in bytes of all ack frame fields without the missing packets or ack
  // blocks.
  static size_t GetMinAckFrameSize(
      QuicTransportVersion version,
      QuicPacketNumberLength largest_observed_length);
  // Size in bytes of a stop waiting frame.
  static size_t GetStopWaitingFrameSize(
      QuicTransportVersion version,
      QuicPacketNumberLength packet_number_length);
  // Size in bytes of all reset stream frame fields.
  static size_t GetRstStreamFrameSize();
  // Size in bytes of all connection close frame fields without the error
  // details and the missing packets from the enclosed ack frame.
  static size_t GetMinConnectionCloseFrameSize();
  // Size in bytes of all GoAway frame fields without the reason phrase.
  static size_t GetMinGoAwayFrameSize();
  // Size in bytes of all WindowUpdate frame fields.
  static size_t GetWindowUpdateFrameSize();
  // Size in bytes of all Blocked frame fields.
  static size_t GetBlockedFrameSize();
  // Size in bytes required to serialize the stream id.
  static size_t GetStreamIdSize(QuicStreamId stream_id);
  // Size in bytes required to serialize the stream offset.
  static size_t GetStreamOffsetSize(QuicTransportVersion version,
                                    QuicStreamOffset offset);
  // Size in bytes required for a serialized version negotiation packet
  static size_t GetVersionNegotiationPacketSize(size_t number_versions);

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
  static QuicStringPiece GetAssociatedDataFromEncryptedPacket(
      QuicTransportVersion version,
      const QuicEncryptedPacket& encrypted,
      QuicConnectionIdLength connection_id_length,
      bool includes_version,
      bool includes_diversification_nonce,
      QuicPacketNumberLength packet_number_length);

  // Serializes a packet containing |frames| into |buffer|.
  // Returns the length of the packet, which must not be longer than
  // |packet_length|.  Returns 0 if it fails to serialize.
  size_t BuildDataPacket(const QuicPacketHeader& header,
                         const QuicFrames& frames,
                         char* buffer,
                         size_t packet_length);

  // Returns a new public reset packet.
  static std::unique_ptr<QuicEncryptedPacket> BuildPublicResetPacket(
      const QuicPublicResetPacket& packet);

  // Returns a new version negotiation packet.
  static std::unique_ptr<QuicEncryptedPacket> BuildVersionNegotiationPacket(
      QuicConnectionId connection_id,
      const QuicTransportVersionVector& versions);

  // If header.public_header.version_flag is set, the version in the
  // packet will be set -- but it will be set from transport_version_ not
  // header.public_header.versions.
  bool AppendPacketHeader(const QuicPacketHeader& header,
                          QuicDataWriter* writer);
  bool AppendTypeByte(const QuicFrame& frame,
                      bool last_frame_in_packet,
                      QuicDataWriter* writer);
  bool AppendStreamFrame(const QuicStreamFrame& frame,
                         bool last_frame_in_packet,
                         QuicDataWriter* writer);

  // SetDecrypter sets the primary decrypter, replacing any that already exists,
  // and takes ownership. If an alternative decrypter is in place then the
  // function DCHECKs. This is intended for cases where one knows that future
  // packets will be using the new decrypter and the previous decrypter is now
  // obsolete. |level| indicates the encryption level of the new decrypter.
  void SetDecrypter(EncryptionLevel level, QuicDecrypter* decrypter);

  // SetAlternativeDecrypter sets a decrypter that may be used to decrypt
  // future packets and takes ownership of it. |level| indicates the encryption
  // level of the decrypter. If |latch_once_used| is true, then the first time
  // that the decrypter is successful it will replace the primary decrypter.
  // Otherwise both decrypters will remain active and the primary decrypter
  // will be the one last used.
  void SetAlternativeDecrypter(EncryptionLevel level,
                               QuicDecrypter* decrypter,
                               bool latch_once_used);

  const QuicDecrypter* decrypter() const;
  const QuicDecrypter* alternative_decrypter() const;

  // Changes the encrypter used for level |level| to |encrypter|. The function
  // takes ownership of |encrypter|.
  void SetEncrypter(EncryptionLevel level, QuicEncrypter* encrypter);

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

  // Returns the maximum length of plaintext that can be encrypted
  // to ciphertext no larger than |ciphertext_size|.
  size_t GetMaxPlaintextSize(size_t ciphertext_size);

  // Let data_producer_ save |data_length| data starts at |iov_offset| in |iov|.
  // TODO(fayang): Remove this method when data is saved before it is consumed.
  void SaveStreamData(QuicStreamId id,
                      QuicIOVector iov,
                      size_t iov_offset,
                      QuicStreamOffset offset,
                      QuicByteCount data_length);

  const std::string& detailed_error() { return detailed_error_; }

  // The minimum packet number length required to represent |packet_number|.
  static QuicPacketNumberLength GetMinPacketNumberLength(
      QuicTransportVersion version,
      QuicPacketNumber packet_number);

  void SetSupportedTransportVersions(
      const QuicTransportVersionVector& versions) {
    supported_versions_ = versions;
    transport_version_ = versions[0];
  }

  // Returns true if data_producer_ is not null.
  bool HasDataProducer() const { return data_producer_ != nullptr; }

  // Returns true if data with |offset| of stream |id| starts with 'CHLO'.
  bool StartsWithChlo(QuicStreamId id, QuicStreamOffset offset) const;

  // Returns byte order to read/write integers and floating numbers.
  Endianness endianness() const;

  void set_validate_flags(bool value) { validate_flags_ = value; }

  Perspective perspective() const { return perspective_; }

  QuicVersionLabel last_version_label() const { return last_version_label_; }

  void set_data_producer(QuicStreamFrameDataProducer* data_producer) {
    data_producer_ = data_producer;
  }

 private:
  friend class test::QuicFramerPeer;

  typedef std::map<QuicPacketNumber, uint8_t> NackRangeMap;

  struct AckFrameInfo {
    AckFrameInfo();
    AckFrameInfo(const AckFrameInfo& other);
    ~AckFrameInfo();

    // The maximum ack block length.
    QuicPacketNumber max_block_length;
    // Length of first ack block.
    QuicPacketNumber first_block_length;
    // Number of ACK blocks needed for the ACK frame.
    size_t num_ack_blocks;
  };

  bool ProcessDataPacket(QuicDataReader* reader,
                         const QuicPacketPublicHeader& public_header,
                         const QuicEncryptedPacket& packet,
                         char* decrypted_buffer,
                         size_t buffer_length);

  bool ProcessPublicResetPacket(QuicDataReader* reader,
                                const QuicPacketPublicHeader& public_header);

  bool ProcessVersionNegotiationPacket(QuicDataReader* reader,
                                       QuicPacketPublicHeader* public_header);

  bool ProcessPublicHeader(QuicDataReader* reader,
                           QuicPacketPublicHeader* header);

  // Processes the unauthenticated portion of the header into |header| from
  // the current QuicDataReader.  Returns true on success, false on failure.
  bool ProcessUnauthenticatedHeader(QuicDataReader* encrypted_reader,
                                    QuicPacketHeader* header);

  // First processes possibly truncated packet number. Calculates the full
  // packet number from the truncated one and the last seen packet number, and
  // stores it to |packet_number|.
  bool ProcessAndCalculatePacketNumber(
      QuicDataReader* reader,
      QuicPacketNumberLength packet_number_length,
      QuicPacketNumber base_packet_number,
      QuicPacketNumber* packet_number);
  bool ProcessFrameData(QuicDataReader* reader, const QuicPacketHeader& header);
  bool ProcessStreamFrame(QuicDataReader* reader,
                          uint8_t frame_type,
                          QuicStreamFrame* frame);
  bool ProcessAckFrame(QuicDataReader* reader,
                       uint8_t frame_type,
                       QuicAckFrame* frame);
  bool ProcessTimestampsInAckFrame(uint8_t num_received_packets,
                                   QuicDataReader* reader,
                                   QuicAckFrame* ack_frame);
  bool ProcessStopWaitingFrame(QuicDataReader* reader,
                               const QuicPacketHeader& public_header,
                               QuicStopWaitingFrame* stop_waiting);
  bool ProcessRstStreamFrame(QuicDataReader* reader, QuicRstStreamFrame* frame);
  bool ProcessConnectionCloseFrame(QuicDataReader* reader,
                                   QuicConnectionCloseFrame* frame);
  bool ProcessGoAwayFrame(QuicDataReader* reader, QuicGoAwayFrame* frame);
  bool ProcessWindowUpdateFrame(QuicDataReader* reader,
                                QuicWindowUpdateFrame* frame);
  bool ProcessBlockedFrame(QuicDataReader* reader, QuicBlockedFrame* frame);
  void ProcessPaddingFrame(QuicDataReader* reader, QuicPaddingFrame* frame);

  bool DecryptPayload(QuicDataReader* encrypted_reader,
                      const QuicPacketHeader& header,
                      const QuicEncryptedPacket& packet,
                      char* decrypted_buffer,
                      size_t buffer_length,
                      size_t* decrypted_length);

  // Sets last_packet_number_. This can only be called after the packet is
  // successfully decrypted.
  void SetLastPacketNumber(const QuicPacketHeader& header);

  // Returns the full packet number from the truncated
  // wire format version and the last seen packet number.
  QuicPacketNumber CalculatePacketNumberFromWire(
      QuicPacketNumberLength packet_number_length,
      QuicPacketNumber base_packet_number,
      QuicPacketNumber packet_number) const;

  // Returns the QuicTime::Delta corresponding to the time from when the framer
  // was created.
  const QuicTime::Delta CalculateTimestampFromWire(uint32_t time_delta_us);

  // Computes the wire size in bytes of time stamps in |ack|.
  size_t GetAckFrameTimeStampSize(const QuicAckFrame& ack);

  // Computes the wire size in bytes of the |ack| frame.
  size_t GetAckFrameSize(const QuicAckFrame& ack,
                         QuicPacketNumberLength packet_number_length);

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
                             QuicPacketNumber length,
                             QuicDataWriter* writer);

  static uint8_t GetPacketNumberFlags(
      QuicPacketNumberLength packet_number_length);

  static AckFrameInfo GetAckFrameInfo(const QuicAckFrame& frame);

  // The Append* methods attempt to write the provided header or frame using the
  // |writer|, and return true if successful.

  bool AppendAckFrameAndTypeByte(const QuicAckFrame& frame,
                                 QuicDataWriter* builder);
  bool AppendTimestampsToAckFrame(const QuicAckFrame& frame,
                                  size_t num_timestamps_offset,
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

  bool RaiseError(QuicErrorCode error);

  void set_error(QuicErrorCode error) { error_ = error; }

  void set_detailed_error(const char* error) { detailed_error_ = error; }

  std::string detailed_error_;
  QuicFramerVisitorInterface* visitor_;
  QuicErrorCode error_;
  // Updated by ProcessPacketHeader when it succeeds.
  QuicPacketNumber last_packet_number_;
  // Updated by ProcessPacketHeader when it succeeds decrypting a larger packet.
  QuicPacketNumber largest_packet_number_;
  // Updated by WritePacketHeader.
  QuicConnectionId last_serialized_connection_id_;
  // The last QUIC version label received.
  QuicVersionLabel last_version_label_;
  // Version of the protocol being used.
  QuicTransportVersion transport_version_;
  // This vector contains QUIC versions which we currently support.
  // This should be ordered such that the highest supported version is the first
  // element, with subsequent elements in descending order (versions can be
  // skipped as necessary).
  QuicTransportVersionVector supported_versions_;
  // Primary decrypter used to decrypt packets during parsing.
  std::unique_ptr<QuicDecrypter> decrypter_;
  // Alternative decrypter that can also be used to decrypt packets.
  std::unique_ptr<QuicDecrypter> alternative_decrypter_;
  // The encryption level of |decrypter_|.
  EncryptionLevel decrypter_level_;
  // The encryption level of |alternative_decrypter_|.
  EncryptionLevel alternative_decrypter_level_;
  // |alternative_decrypter_latch_| is true if, when |alternative_decrypter_|
  // successfully decrypts a packet, we should install it as the only
  // decrypter.
  bool alternative_decrypter_latch_;
  // Encrypters used to encrypt packets via EncryptPayload().
  std::unique_ptr<QuicEncrypter> encrypter_[NUM_ENCRYPTION_LEVELS];
  // Tracks if the framer is being used by the entity that received the
  // connection or the entity that initiated it.
  Perspective perspective_;
  // If false, skip validation that the public flags are set to legal values.
  bool validate_flags_;
  // The time this framer was created.  Time written to the wire will be
  // written as a delta from this value.
  QuicTime creation_time_;
  // The time delta computed for the last timestamp frame. This is relative to
  // the creation_time.
  QuicTime::Delta last_timestamp_;
  // The diversification nonce from the last received packet.
  DiversificationNonce last_nonce_;

  // If not null, framer asks data_producer_ to write stream frame data. Not
  // owned.
  QuicStreamFrameDataProducer* data_producer_;

  DISALLOW_COPY_AND_ASSIGN(QuicFramer);
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_FRAMER_H_
