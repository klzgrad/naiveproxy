// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Accumulates frames for the next packet until more frames no longer fit or
// it's time to create a packet from them.

#ifndef NET_QUIC_CORE_QUIC_PACKET_CREATOR_H_
#define NET_QUIC_CORE_QUIC_PACKET_CREATOR_H_

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/macros.h"
#include "net/quic/core/quic_connection_close_delegate_interface.h"
#include "net/quic/core/quic_framer.h"
#include "net/quic/core/quic_iovector.h"
#include "net/quic/core/quic_packets.h"
#include "net/quic/core/quic_pending_retransmission.h"
#include "net/quic/platform/api/quic_export.h"

namespace net {
namespace test {
class QuicPacketCreatorPeer;
}

class QUIC_EXPORT_PRIVATE QuicPacketCreator {
 public:
  // A delegate interface for further processing serialized packet.
  class QUIC_EXPORT_PRIVATE DelegateInterface
      : public QuicConnectionCloseDelegateInterface {
   public:
    ~DelegateInterface() override {}
    // Called when a packet is serialized. Delegate does not take the ownership
    // of |serialized_packet|, but takes ownership of any frames it removes
    // from |packet.retransmittable_frames|.
    virtual void OnSerializedPacket(SerializedPacket* serialized_packet) = 0;
  };

  // Interface which gets callbacks from the QuicPacketCreator at interesting
  // points.  Implementations must not mutate the state of the creator
  // as a result of these callbacks.
  class QUIC_EXPORT_PRIVATE DebugDelegate {
   public:
    virtual ~DebugDelegate() {}

    // Called when a frame has been added to the current packet.
    virtual void OnFrameAddedToPacket(const QuicFrame& frame) {}
  };

  QuicPacketCreator(QuicConnectionId connection_id,
                    QuicFramer* framer,
                    QuicBufferAllocator* buffer_allocator,
                    DelegateInterface* delegate);

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

  // The overhead the framing will add for a packet with one frame.
  static size_t StreamFramePacketOverhead(
      QuicTransportVersion version,
      QuicConnectionIdLength connection_id_length,
      bool include_version,
      bool include_diversification_nonce,
      QuicPacketNumberLength packet_number_length,
      QuicStreamOffset offset);

  // Returns false and flushes all pending frames if current open packet is
  // full.
  // If current packet is not full, converts a raw payload into a stream frame
  // that fits into the open packet and adds it to the packet.
  // The payload begins at |iov_offset| into the |iov|.
  bool ConsumeData(QuicStreamId id,
                   QuicIOVector iov,
                   size_t iov_offset,
                   QuicStreamOffset offset,
                   bool fin,
                   bool needs_full_padding,
                   QuicFrame* frame);

  // Returns true if current open packet can accommodate more stream frames of
  // stream |id| at |offset|, false otherwise.
  bool HasRoomForStreamFrame(QuicStreamId id, QuicStreamOffset offset);

  // Re-serializes frames with the original packet's packet number length.
  // Used for retransmitting packets to ensure they aren't too long.
  void ReserializeAllFrames(const QuicPendingRetransmission& retransmission,
                            char* buffer,
                            size_t buffer_len);

  // Serializes all added frames into a single packet and invokes the delegate_
  // to further process the SerializedPacket.
  void Flush();

  // Optimized method to create a QuicStreamFrame and serialize it. Adds the
  // QuicStreamFrame to the returned SerializedPacket.  Sets
  // |num_bytes_consumed| to the number of bytes consumed to create the
  // QuicStreamFrame.
  void CreateAndSerializeStreamFrame(
      QuicStreamId id,
      const QuicIOVector& iov,
      QuicStreamOffset iov_offset,
      QuicStreamOffset stream_offset,
      bool fin,
      QuicReferenceCountedPointer<QuicAckListenerInterface> listener,
      size_t* num_bytes_consumed);

  // Returns true if there are frames pending to be serialized.
  bool HasPendingFrames() const;

  // Returns true if there are retransmittable frames pending to be serialized.
  bool HasPendingRetransmittableFrames() const;

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
  bool AddSavedFrame(const QuicFrame& frame);

  // Identical to AddSavedFrame, but allows the frame to be padded.
  bool AddPaddedSavedFrame(const QuicFrame& frame);

  // Adds |listener| to the next serialized packet and notifies the listener
  // with |length| as the number of acked bytes.
  void AddAckListener(
      QuicReferenceCountedPointer<QuicAckListenerInterface> listener,
      QuicPacketLength length);

  // Creates a version negotiation packet which supports |supported_versions|.
  std::unique_ptr<QuicEncryptedPacket> SerializeVersionNegotiationPacket(
      const QuicTransportVersionVector& supported_versions);

  // Returns a dummy packet that is valid but contains no useful information.
  static SerializedPacket NoPacket();

  // Sets the encryption level that will be applied to new packets.
  void set_encryption_level(EncryptionLevel level) {
    packet_.encryption_level = level;
  }

  // packet number of the last created packet, or 0 if no packets have been
  // created.
  QuicPacketNumber packet_number() const { return packet_.packet_number; }

  QuicConnectionIdLength connection_id_length() const {
    return connection_id_length_;
  }

  void set_connection_id_length(QuicConnectionIdLength length) {
    connection_id_length_ = length;
  }

  QuicByteCount max_packet_length() const { return max_packet_length_; }

  bool has_ack() const { return packet_.has_ack; }

  bool has_stop_waiting() const { return packet_.has_stop_waiting; }

  // Sets the encrypter to use for the encryption level and updates the max
  // plaintext size.
  void SetEncrypter(EncryptionLevel level, QuicEncrypter* encrypter);

  // Indicates whether the packet creator is in a state where it can change
  // current maximum packet length.
  bool CanSetMaxPacketLength() const;

  // Sets the maximum packet length.
  void SetMaxPacketLength(QuicByteCount length);

  // Increases pending_padding_bytes by |size|. Pending padding will be sent by
  // MaybeAddPadding().
  void AddPendingPadding(QuicByteCount size);

  void set_debug_delegate(DebugDelegate* debug_delegate) {
    debug_delegate_ = debug_delegate;
  }

  QuicByteCount pending_padding_bytes() const { return pending_padding_bytes_; }

 private:
  friend class test::QuicPacketCreatorPeer;

  static bool ShouldRetransmit(const QuicFrame& frame);

  // Converts a raw payload to a frame which fits into the current open
  // packet.  The payload begins at |iov_offset| into the |iov|.
  // If data is empty and fin is true, the expected behavior is to consume the
  // fin but return 0.  If any data is consumed, it will be copied into a
  // new buffer that |frame| will point to and own.
  void CreateStreamFrame(QuicStreamId id,
                         QuicIOVector iov,
                         size_t iov_offset,
                         QuicStreamOffset offset,
                         bool fin,
                         QuicFrame* frame);

  void FillPacketHeader(QuicPacketHeader* header);

  // Adds a |frame| if there is space and returns false and flushes all pending
  // frames if there isn't room. If |save_retransmittable_frames| is true,
  // saves the |frame| in the next SerializedPacket.
  bool AddFrame(const QuicFrame& frame, bool save_retransmittable_frames);

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

  // Returns true if a diversification nonce should be included in the current
  // packet's public header.
  bool IncludeNonceInPublicHeader();

  // Returns true if |frame| starts with CHLO.
  bool StreamFrameStartsWithChlo(QuicIOVector iov,
                                 size_t iov_offset,
                                 const QuicStreamFrame& frame) const;

  // Does not own these delegates or the framer.
  DelegateInterface* delegate_;
  DebugDelegate* debug_delegate_;
  QuicFramer* framer_;

  QuicBufferAllocator* const buffer_allocator_;

  // Controls whether version should be included while serializing the packet.
  bool send_version_in_packet_;
  // If true, then |nonce_for_public_header_| will be included in the public
  // header of all packets created at the initial encryption level.
  bool have_diversification_nonce_;
  DiversificationNonce diversification_nonce_;
  // Maximum length including headers and encryption (UDP payload length.)
  QuicByteCount max_packet_length_;
  size_t max_plaintext_size_;
  // Length of connection_id to send over the wire.
  QuicConnectionIdLength connection_id_length_;

  // Frames to be added to the next SerializedPacket
  QuicFrames queued_frames_;

  // packet_size should never be read directly, use PacketSize() instead.
  // TODO(ianswett): Move packet_size_ into SerializedPacket once
  // QuicEncryptedPacket has been flattened into SerializedPacket.
  size_t packet_size_;
  QuicConnectionId connection_id_;

  // Packet used to invoke OnSerializedPacket.
  SerializedPacket packet_;

  // Pending padding bytes to send. Pending padding bytes will be sent in next
  // packet(s) (after all other frames) if current constructed packet does not
  // have room to send all of them.
  QuicByteCount pending_padding_bytes_;

  // Indicates whether current constructed packet needs full padding to max
  // packet size. Please note, full padding does not consume pending padding
  // bytes.
  bool needs_full_padding_;

  DISALLOW_COPY_AND_ASSIGN(QuicPacketCreator);
};

}  // namespace net

#endif  // NET_QUIC_CORE_QUIC_PACKET_CREATOR_H_
