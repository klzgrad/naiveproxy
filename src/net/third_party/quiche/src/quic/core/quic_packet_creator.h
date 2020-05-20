// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Responsible for creating packets on behalf of a QuicConnection.
// Packets are serialized just-in-time. Stream data and control frames will be
// requested from the Connection just-in-time. Frames are accumulated into
// "current" packet until no more frames can fit, then current packet gets
// serialized and passed to connection via OnSerializedPacket().
//
// Whether a packet should be serialized is determined by whether delegate is
// writable. If the Delegate is not writable, then no operations will cause
// a packet to be serialized.

#ifndef QUICHE_QUIC_CORE_QUIC_PACKET_CREATOR_H_
#define QUICHE_QUIC_CORE_QUIC_PACKET_CREATOR_H_

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "net/third_party/quiche/src/quic/core/frames/quic_stream_frame.h"
#include "net/third_party/quiche/src/quic/core/quic_circular_deque.h"
#include "net/third_party/quiche/src/quic/core/quic_coalesced_packet.h"
#include "net/third_party/quiche/src/quic/core/quic_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {
namespace test {
class QuicPacketCreatorPeer;
}

class QUIC_EXPORT_PRIVATE QuicPacketCreator {
 public:
  // A delegate interface for further processing serialized packet.
  class QUIC_EXPORT_PRIVATE DelegateInterface {
   public:
    virtual ~DelegateInterface() {}
    // Get a buffer of kMaxOutgoingPacketSize bytes to serialize the next
    // packet. If return nullptr, QuicPacketCreator will serialize on a stack
    // buffer.
    virtual char* GetPacketBuffer() = 0;
    // Called when a packet is serialized. Delegate does not take the ownership
    // of |serialized_packet|, but takes ownership of any frames it removes
    // from |packet.retransmittable_frames|.
    virtual void OnSerializedPacket(SerializedPacket* serialized_packet) = 0;

    // Called when an unrecoverable error is encountered.
    virtual void OnUnrecoverableError(QuicErrorCode error,
                                      const std::string& error_details) = 0;

    // Consults delegate whether a packet should be generated.
    virtual bool ShouldGeneratePacket(HasRetransmittableData retransmittable,
                                      IsHandshake handshake) = 0;
    // Called when there is data to be sent. Retrieves updated ACK frame from
    // the delegate.
    virtual const QuicFrames MaybeBundleAckOpportunistically() = 0;
  };

  // Interface which gets callbacks from the QuicPacketCreator at interesting
  // points.  Implementations must not mutate the state of the creator
  // as a result of these callbacks.
  class QUIC_EXPORT_PRIVATE DebugDelegate {
   public:
    virtual ~DebugDelegate() {}

    // Called when a frame has been added to the current packet.
    virtual void OnFrameAddedToPacket(const QuicFrame& /*frame*/) {}

    // Called when a stream frame is coalesced with an existing stream frame.
    // |frame| is the new stream frame.
    virtual void OnStreamFrameCoalesced(const QuicStreamFrame& /*frame*/) {}
  };

  QuicPacketCreator(QuicConnectionId server_connection_id,
                    QuicFramer* framer,
                    DelegateInterface* delegate);
  QuicPacketCreator(QuicConnectionId server_connection_id,
                    QuicFramer* framer,
                    QuicRandom* random,
                    DelegateInterface* delegate);
  QuicPacketCreator(const QuicPacketCreator&) = delete;
  QuicPacketCreator& operator=(const QuicPacketCreator&) = delete;

  ~QuicPacketCreator();

  // Makes the framer not serialize the protocol version in sent packets.
  void StopSendingVersion();

  // SetDiversificationNonce sets the nonce that will be sent in each public
  // header of packets encrypted at the initial encryption level. Should only
  // be called by servers.
  void SetDiversificationNonce(const DiversificationNonce& nonce);

  // Update the packet number length to use in future packets as soon as it
  // can be safely changed.
  // TODO(fayang): Directly set packet number length instead of compute it in
  // creator.
  void UpdatePacketNumberLength(QuicPacketNumber least_packet_awaited_by_peer,
                                QuicPacketCount max_packets_in_flight);

  // Skip |count| packet numbers.
  void SkipNPacketNumbers(QuicPacketCount count,
                          QuicPacketNumber least_packet_awaited_by_peer,
                          QuicPacketCount max_packets_in_flight);

  // The overhead the framing will add for a packet with one frame.
  static size_t StreamFramePacketOverhead(
      QuicTransportVersion version,
      QuicConnectionIdLength destination_connection_id_length,
      QuicConnectionIdLength source_connection_id_length,
      bool include_version,
      bool include_diversification_nonce,
      QuicPacketNumberLength packet_number_length,
      QuicVariableLengthIntegerLength retry_token_length_length,
      QuicVariableLengthIntegerLength length_length,
      QuicStreamOffset offset);

  // Returns false and flushes all pending frames if current open packet is
  // full.
  // If current packet is not full, creates a stream frame that fits into the
  // open packet and adds it to the packet.
  bool ConsumeDataToFillCurrentPacket(QuicStreamId id,
                                      size_t data_size,
                                      QuicStreamOffset offset,
                                      bool fin,
                                      bool needs_full_padding,
                                      TransmissionType transmission_type,
                                      QuicFrame* frame);

  // Creates a CRYPTO frame that fits into the current packet (which must be
  // empty) and adds it to the packet.
  bool ConsumeCryptoDataToFillCurrentPacket(EncryptionLevel level,
                                            size_t write_length,
                                            QuicStreamOffset offset,
                                            bool needs_full_padding,
                                            TransmissionType transmission_type,
                                            QuicFrame* frame);

  // Returns true if current open packet can accommodate more stream frames of
  // stream |id| at |offset| and data length |data_size|, false otherwise.
  bool HasRoomForStreamFrame(QuicStreamId id,
                             QuicStreamOffset offset,
                             size_t data_size);

  // Returns true if current open packet can accommodate a message frame of
  // |length|.
  bool HasRoomForMessageFrame(QuicByteCount length);

  // Serializes all added frames into a single packet and invokes the delegate_
  // to further process the SerializedPacket.
  void FlushCurrentPacket();

  // Optimized method to create a QuicStreamFrame and serialize it. Adds the
  // QuicStreamFrame to the returned SerializedPacket.  Sets
  // |num_bytes_consumed| to the number of bytes consumed to create the
  // QuicStreamFrame.
  void CreateAndSerializeStreamFrame(QuicStreamId id,
                                     size_t write_length,
                                     QuicStreamOffset iov_offset,
                                     QuicStreamOffset stream_offset,
                                     bool fin,
                                     TransmissionType transmission_type,
                                     size_t* num_bytes_consumed);

  // Returns true if there are frames pending to be serialized.
  bool HasPendingFrames() const;

  // Returns true if there are retransmittable frames pending to be serialized.
  bool HasPendingRetransmittableFrames() const;

  // Returns true if there are stream frames for |id| pending to be serialized.
  bool HasPendingStreamFramesOfStream(QuicStreamId id) const;

  // Returns the number of bytes which are available to be used by additional
  // frames in the packet.  Since stream frames are slightly smaller when they
  // are the last frame in a packet, this method will return a different
  // value than max_packet_size - PacketSize(), in this case.
  size_t BytesFree();

  // Returns the number of bytes that the packet will expand by if a new frame
  // is added to the packet. If the last frame was a stream frame, it will
  // expand slightly when a new frame is added, and this method returns the
  // amount of expected expansion.
  size_t ExpansionOnNewFrame() const;

  // Returns the number of bytes in the current packet, including the header,
  // if serialized with the current frames.  Adding a frame to the packet
  // may change the serialized length of existing frames, as per the comment
  // in BytesFree.
  size_t PacketSize();

  // Tries to add |frame| to the packet creator's list of frames to be
  // serialized. If the frame does not fit into the current packet, flushes the
  // packet and returns false.
  bool AddFrame(const QuicFrame& frame, TransmissionType transmission_type);

  // Identical to AddSavedFrame, but allows the frame to be padded.
  bool AddPaddedSavedFrame(const QuicFrame& frame,
                           TransmissionType transmission_type);

  // Creates a version negotiation packet which supports |supported_versions|.
  std::unique_ptr<QuicEncryptedPacket> SerializeVersionNegotiationPacket(
      bool ietf_quic,
      bool use_length_prefix,
      const ParsedQuicVersionVector& supported_versions);

  // Creates a connectivity probing packet for versions prior to version 99.
  OwningSerializedPacketPointer SerializeConnectivityProbingPacket();

  // Create connectivity probing request and response packets using PATH
  // CHALLENGE and PATH RESPONSE frames, respectively, for version 99/IETF QUIC.
  // SerializePathChallengeConnectivityProbingPacket will pad the packet to be
  // MTU bytes long.
  OwningSerializedPacketPointer SerializePathChallengeConnectivityProbingPacket(
      QuicPathFrameBuffer* payload);

  // If |is_padded| is true then SerializePathResponseConnectivityProbingPacket
  // will pad the packet to be MTU bytes long, else it will not pad the packet.
  // |payloads| is cleared.
  OwningSerializedPacketPointer SerializePathResponseConnectivityProbingPacket(
      const QuicCircularDeque<QuicPathFrameBuffer>& payloads,
      const bool is_padded);

  // Returns a dummy packet that is valid but contains no useful information.
  static SerializedPacket NoPacket();

  // Returns the destination connection ID to send over the wire.
  QuicConnectionId GetDestinationConnectionId() const;

  // Returns the source connection ID to send over the wire.
  QuicConnectionId GetSourceConnectionId() const;

  // Returns length of destination connection ID to send over the wire.
  QuicConnectionIdLength GetDestinationConnectionIdLength() const;

  // Returns length of source connection ID to send over the wire.
  QuicConnectionIdLength GetSourceConnectionIdLength() const;

  // Sets whether the server connection ID should be sent over the wire.
  void SetServerConnectionIdIncluded(
      QuicConnectionIdIncluded server_connection_id_included);

  // Update the server connection ID used in outgoing packets.
  void SetServerConnectionId(QuicConnectionId server_connection_id);

  // Update the client connection ID used in outgoing packets.
  void SetClientConnectionId(QuicConnectionId client_connection_id);

  // Sets the encryption level that will be applied to new packets.
  void set_encryption_level(EncryptionLevel level) {
    packet_.encryption_level = level;
  }

  // packet number of the last created packet, or 0 if no packets have been
  // created.
  QuicPacketNumber packet_number() const { return packet_.packet_number; }

  QuicByteCount max_packet_length() const { return max_packet_length_; }

  bool has_ack() const { return packet_.has_ack; }

  bool has_stop_waiting() const { return packet_.has_stop_waiting; }

  // Sets the encrypter to use for the encryption level and updates the max
  // plaintext size.
  void SetEncrypter(EncryptionLevel level,
                    std::unique_ptr<QuicEncrypter> encrypter);

  // Indicates whether the packet creator is in a state where it can change
  // current maximum packet length.
  bool CanSetMaxPacketLength() const;

  // Sets the maximum packet length.
  void SetMaxPacketLength(QuicByteCount length);

  // Set a soft maximum packet length in the creator. If a packet cannot be
  // successfully created, creator will remove the soft limit and use the actual
  // max packet length.
  void SetSoftMaxPacketLength(QuicByteCount length);

  // Increases pending_padding_bytes by |size|. Pending padding will be sent by
  // MaybeAddPadding().
  void AddPendingPadding(QuicByteCount size);

  // Sets the retry token to be sent over the wire in IETF Initial packets.
  void SetRetryToken(quiche::QuicheStringPiece retry_token);

  // Consumes retransmittable control |frame|. Returns true if the frame is
  // successfully consumed. Returns false otherwise.
  bool ConsumeRetransmittableControlFrame(const QuicFrame& frame);

  // Given some data, may consume part or all of it and pass it to the
  // packet creator to be serialized into packets. If not in batch
  // mode, these packets will also be sent during this call.
  // When |state| is FIN_AND_PADDING, random padding of size [1, 256] will be
  // added after stream frames. If current constructed packet cannot
  // accommodate, the padding will overflow to the next packet(s).
  QuicConsumedData ConsumeData(QuicStreamId id,
                               size_t write_length,
                               QuicStreamOffset offset,
                               StreamSendingState state);

  // Sends as many data only packets as allowed by the send algorithm and the
  // available iov.
  // This path does not support padding, or bundling pending frames.
  // In case we access this method from ConsumeData, total_bytes_consumed
  // keeps track of how many bytes have already been consumed.
  QuicConsumedData ConsumeDataFastPath(QuicStreamId id,
                                       size_t write_length,
                                       QuicStreamOffset offset,
                                       bool fin,
                                       size_t total_bytes_consumed);

  // Consumes data for CRYPTO frames sent at |level| starting at |offset| for a
  // total of |write_length| bytes, and returns the number of bytes consumed.
  // The data is passed into the packet creator and serialized into one or more
  // packets.
  size_t ConsumeCryptoData(EncryptionLevel level,
                           size_t write_length,
                           QuicStreamOffset offset);

  // Generates an MTU discovery packet of specified size.
  void GenerateMtuDiscoveryPacket(QuicByteCount target_mtu);

  // Called when there is data to be sent, Retrieves updated ACK frame from
  // delegate_ and flushes it.
  void MaybeBundleAckOpportunistically();

  // Called to flush ACK and STOP_WAITING frames, returns false if the flush
  // fails.
  bool FlushAckFrame(const QuicFrames& frames);

  // Adds a random amount of padding (between 1 to 256 bytes).
  void AddRandomPadding();

  // Attaches packet flusher.
  void AttachPacketFlusher();

  // Flushes everything, including current open packet and pending padding.
  void Flush();

  // Sends remaining pending padding.
  // Pending paddings should only be sent when there is nothing else to send.
  void SendRemainingPendingPadding();

  // Set the minimum number of bytes for the server connection id length;
  void SetServerConnectionIdLength(uint32_t length);

  // Set transmission type of next constructed packets.
  void SetTransmissionType(TransmissionType type);

  // Tries to add a message frame containing |message| and returns the status.
  MessageStatus AddMessageFrame(QuicMessageId message_id,
                                QuicMemSliceSpan message);

  // Returns the largest payload that will fit into a single MESSAGE frame.
  QuicPacketLength GetCurrentLargestMessagePayload() const;
  // Returns the largest payload that will fit into a single MESSAGE frame at
  // any point during the connection.  This assumes the version and
  // connection ID lengths do not change.
  QuicPacketLength GetGuaranteedLargestMessagePayload() const;

  // Packet number of next created packet.
  QuicPacketNumber NextSendingPacketNumber() const;

  void set_debug_delegate(DebugDelegate* debug_delegate) {
    debug_delegate_ = debug_delegate;
  }

  QuicByteCount pending_padding_bytes() const { return pending_padding_bytes_; }

  QuicTransportVersion transport_version() const {
    return framer_->transport_version();
  }

  // Returns the minimum size that the plaintext of a packet must be.
  static size_t MinPlaintextPacketSize(const ParsedQuicVersion& version);

  // Indicates whether packet flusher is currently attached.
  bool PacketFlusherAttached() const;

  void set_fully_pad_crypto_handshake_packets(bool new_value) {
    fully_pad_crypto_handshake_packets_ = new_value;
  }

  bool fully_pad_crypto_handshake_packets() const {
    return fully_pad_crypto_handshake_packets_;
  }

  // Serialize a probing packet that uses IETF QUIC's PATH CHALLENGE frame. Also
  // fills the packet with padding.
  size_t BuildPaddedPathChallengePacket(const QuicPacketHeader& header,
                                        char* buffer,
                                        size_t packet_length,
                                        QuicPathFrameBuffer* payload,
                                        QuicRandom* randomizer,
                                        EncryptionLevel level);

  // Serialize a probing response packet that uses IETF QUIC's PATH RESPONSE
  // frame. Also fills the packet with padding if |is_padded| is
  // true. |payloads| is always emptied, even if the packet can not be
  // successfully built.
  size_t BuildPathResponsePacket(
      const QuicPacketHeader& header,
      char* buffer,
      size_t packet_length,
      const QuicCircularDeque<QuicPathFrameBuffer>& payloads,
      const bool is_padded,
      EncryptionLevel level);

  // Serializes a probing packet, which is a padded PING packet. Returns the
  // length of the packet. Returns 0 if it fails to serialize.
  size_t BuildConnectivityProbingPacket(const QuicPacketHeader& header,
                                        char* buffer,
                                        size_t packet_length,
                                        EncryptionLevel level);

  // Serializes |coalesced| to provided |buffer|, returns coalesced packet
  // length if serialization succeeds. Otherwise, returns 0.
  size_t SerializeCoalescedPacket(const QuicCoalescedPacket& coalesced,
                                  char* buffer,
                                  size_t buffer_len);

 private:
  friend class test::QuicPacketCreatorPeer;

  // Creates a stream frame which fits into the current open packet. If
  // |data_size| is 0 and fin is true, the expected behavior is to consume
  // the fin.
  void CreateStreamFrame(QuicStreamId id,
                         size_t data_size,
                         QuicStreamOffset offset,
                         bool fin,
                         QuicFrame* frame);

  // Creates a CRYPTO frame which fits into the current open packet. Returns
  // false if there isn't enough room in the current open packet for a CRYPTO
  // frame, and true if there is.
  bool CreateCryptoFrame(EncryptionLevel level,
                         size_t write_length,
                         QuicStreamOffset offset,
                         QuicFrame* frame);

  void FillPacketHeader(QuicPacketHeader* header);

  // Adds a padding frame to the current packet (if there is space) when (1)
  // current packet needs full padding or (2) there are pending paddings.
  void MaybeAddPadding();

  // Serializes all frames which have been added and adds any which should be
  // retransmitted to packet_.retransmittable_frames. All frames must fit into
  // a single packet.
  // Fails if |buffer_len| isn't long enough for the encrypted packet.
  void SerializePacket(char* encrypted_buffer, size_t buffer_len);

  // Called after a new SerialiedPacket is created to call the delegate's
  // OnSerializedPacket and reset state.
  void OnSerializedPacket();

  // Clears all fields of packet_ that should be cleared between serializations.
  void ClearPacket();

  // Re-serialzes frames of ENCRYPTION_INITIAL packet in coalesced packet with
  // the original packet's packet number and packet number length.
  // |padding_size| indicates the size of necessary padding. Returns 0 if
  // serialization fails.
  size_t ReserializeInitialPacketInCoalescedPacket(
      const SerializedPacket& packet,
      size_t padding_size,
      char* buffer,
      size_t buffer_len);

  // Tries to coalesce |frame| with the back of |queued_frames_|.
  // Returns true on success.
  bool MaybeCoalesceStreamFrame(const QuicStreamFrame& frame);

  // Called to remove the soft max_packet_length and restores
  // latched_hard_max_packet_length_ if the packet cannot accommodate a single
  // frame. Returns true if the soft limit is successfully removed. Returns
  // false if either there is no current soft limit or there are queued frames
  // (such that the packet length cannot be changed).
  bool RemoveSoftMaxPacketLength();

  // Returns true if a diversification nonce should be included in the current
  // packet's header.
  bool IncludeNonceInPublicHeader() const;

  // Returns true if version should be included in current packet's header.
  bool IncludeVersionInHeader() const;

  // Returns length of packet number to send over the wire.
  // packet_.packet_number_length should never be read directly, use this
  // function instead.
  QuicPacketNumberLength GetPacketNumberLength() const;

  // Returns the size in bytes of the packet header.
  size_t PacketHeaderSize() const;

  // Returns whether the destination connection ID is sent over the wire.
  QuicConnectionIdIncluded GetDestinationConnectionIdIncluded() const;

  // Returns whether the source connection ID is sent over the wire.
  QuicConnectionIdIncluded GetSourceConnectionIdIncluded() const;

  // Returns length of the retry token variable length integer to send over the
  // wire. Is non-zero for v99 IETF Initial packets.
  QuicVariableLengthIntegerLength GetRetryTokenLengthLength() const;

  // Returns the retry token to send over the wire, only sent in
  // v99 IETF Initial packets.
  quiche::QuicheStringPiece GetRetryToken() const;

  // Returns length of the length variable length integer to send over the
  // wire. Is non-zero for v99 IETF Initial, 0-RTT or Handshake packets.
  QuicVariableLengthIntegerLength GetLengthLength() const;

  // Returns true if |frame| is a ClientHello.
  bool StreamFrameIsClientHello(const QuicStreamFrame& frame) const;

  // Returns true if packet under construction has IETF long header.
  bool HasIetfLongHeader() const;

  // Does not own these delegates or the framer.
  DelegateInterface* delegate_;
  DebugDelegate* debug_delegate_;
  QuicFramer* framer_;
  QuicRandom* random_;

  // Controls whether version should be included while serializing the packet.
  // send_version_in_packet_ should never be read directly, use
  // IncludeVersionInHeader() instead.
  bool send_version_in_packet_;
  // If true, then |diversification_nonce_| will be included in the header of
  // all packets created at the initial encryption level.
  bool have_diversification_nonce_;
  DiversificationNonce diversification_nonce_;
  // Maximum length including headers and encryption (UDP payload length.)
  QuicByteCount max_packet_length_;
  size_t max_plaintext_size_;
  // Whether the server_connection_id is sent over the wire.
  QuicConnectionIdIncluded server_connection_id_included_;

  // Frames to be added to the next SerializedPacket
  QuicFrames queued_frames_;

  // packet_size should never be read directly, use PacketSize() instead.
  // TODO(ianswett): Move packet_size_ into SerializedPacket once
  // QuicEncryptedPacket has been flattened into SerializedPacket.
  size_t packet_size_;
  QuicConnectionId server_connection_id_;
  QuicConnectionId client_connection_id_;

  // Packet used to invoke OnSerializedPacket.
  SerializedPacket packet_;

  // Retry token to send over the wire in v99 IETF Initial packets.
  std::string retry_token_;

  // Pending padding bytes to send. Pending padding bytes will be sent in next
  // packet(s) (after all other frames) if current constructed packet does not
  // have room to send all of them.
  QuicByteCount pending_padding_bytes_;

  // Indicates whether current constructed packet needs full padding to max
  // packet size. Please note, full padding does not consume pending padding
  // bytes.
  bool needs_full_padding_;

  // Transmission type of the next serialized packet.
  TransmissionType next_transmission_type_;

  // True if packet flusher is currently attached.
  bool flusher_attached_;

  // Whether crypto handshake packets should be fully padded.
  bool fully_pad_crypto_handshake_packets_;

  // Packet number of the first packet of a write operation. This gets set
  // when the out-most flusher attaches and gets cleared when the out-most
  // flusher detaches.
  QuicPacketNumber write_start_packet_number_;

  // If not 0, this latches the actual max_packet_length when
  // SetSoftMaxPacketLength is called and max_packet_length_ gets
  // set to a soft value.
  QuicByteCount latched_hard_max_packet_length_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_PACKET_CREATOR_H_
