// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The base class for client/server QUIC streams.

// It does not contain the entire interface needed by an application to interact
// with a QUIC stream.  Some parts of the interface must be obtained by
// accessing the owning session object.  A subclass of QuicStream
// connects the object and the application that generates and consumes the data
// of the stream.

// The QuicStream object has a dependent QuicStreamSequencer object,
// which is given the stream frames as they arrive, and provides stream data in
// order by invoking ProcessRawData().

#ifndef QUICHE_QUIC_CORE_QUIC_STREAM_H_
#define QUICHE_QUIC_CORE_QUIC_STREAM_H_

#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/frames/quic_rst_stream_frame.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_flow_controller.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_stream_priority.h"
#include "quiche/quic/core/quic_stream_send_buffer.h"
#include "quiche/quic/core/quic_stream_sequencer.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/session_notifier_interface.h"
#include "quiche/quic/core/stream_delegate_interface.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/common/platform/api/quiche_reference_counted.h"
#include "quiche/spdy/core/spdy_protocol.h"

namespace quic {

namespace test {
class QuicStreamPeer;
}  // namespace test

class QuicSession;
class QuicStream;

// Buffers frames for a stream until the first byte of that frame arrives.
class QUICHE_EXPORT PendingStream
    : public QuicStreamSequencer::StreamInterface {
 public:
  PendingStream(QuicStreamId id, QuicSession* session);
  PendingStream(const PendingStream&) = delete;
  PendingStream(PendingStream&&) = default;
  ~PendingStream() override = default;

  // QuicStreamSequencer::StreamInterface
  void OnDataAvailable() override;
  void OnFinRead() override;
  void AddBytesConsumed(QuicByteCount bytes) override;
  void ResetWithError(QuicResetStreamError error) override;
  void OnUnrecoverableError(QuicErrorCode error,
                            const std::string& details) override;
  void OnUnrecoverableError(QuicErrorCode error,
                            QuicIetfTransportErrorCodes ietf_error,
                            const std::string& details) override;
  QuicStreamId id() const override;
  ParsedQuicVersion version() const override;

  // Buffers the contents of |frame|. Frame must have a non-zero offset.
  // If the data violates flow control, the connection will be closed.
  void OnStreamFrame(const QuicStreamFrame& frame);

  bool is_bidirectional() const { return is_bidirectional_; }

  // Stores the final byte offset from |frame|.
  // If the final offset violates flow control, the connection will be closed.
  void OnRstStreamFrame(const QuicRstStreamFrame& frame);

  void OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame);

  void OnStopSending(QuicResetStreamError stop_sending_error_code);

  // The error code received from QuicStopSendingFrame (if any).
  const std::optional<QuicResetStreamError>& GetStopSendingErrorCode() const {
    return stop_sending_error_code_;
  }

  // Returns the number of bytes read on this stream.
  uint64_t stream_bytes_read() { return stream_bytes_read_; }

  const QuicStreamSequencer* sequencer() const { return &sequencer_; }

  void MarkConsumed(QuicByteCount num_bytes);

  // Tells the sequencer to ignore all incoming data itself and not call
  // OnDataAvailable().
  void StopReading();

  QuicTime creation_time() const { return creation_time_; }

 private:
  friend class QuicStream;

  bool MaybeIncreaseHighestReceivedOffset(QuicStreamOffset new_offset);

  // ID of this stream.
  QuicStreamId id_;

  // QUIC version being used by this stream.
  ParsedQuicVersion version_;

  // |stream_delegate_| must outlive this stream.
  StreamDelegateInterface* stream_delegate_;

  // Bytes read refers to payload bytes only: they do not include framing,
  // encryption overhead etc.
  uint64_t stream_bytes_read_;

  // True if a frame containing a fin has been received.
  bool fin_received_;

  // True if this pending stream is backing a bidirectional stream.
  bool is_bidirectional_;

  // Connection-level flow controller. Owned by the session.
  QuicFlowController* connection_flow_controller_;
  // Stream-level flow controller.
  QuicFlowController flow_controller_;
  // Stores the buffered frames.
  QuicStreamSequencer sequencer_;
  // The error code received from QuicStopSendingFrame (if any).
  std::optional<QuicResetStreamError> stop_sending_error_code_;
  // The time when this pending stream is created.
  const QuicTime creation_time_;
};

class QUICHE_EXPORT QuicStream : public QuicStreamSequencer::StreamInterface {
 public:
  // Creates a new stream with stream_id |id| associated with |session|. If
  // |is_static| is true, then the stream will be given precedence
  // over other streams when determing what streams should write next.
  // |type| indicates whether the stream is bidirectional, read unidirectional
  // or write unidirectional.
  // TODO(fayang): Remove |type| when IETF stream ID numbering fully kicks in.
  QuicStream(QuicStreamId id, QuicSession* session, bool is_static,
             StreamType type);
  QuicStream(PendingStream* pending, QuicSession* session, bool is_static);
  QuicStream(const QuicStream&) = delete;
  QuicStream& operator=(const QuicStream&) = delete;

  virtual ~QuicStream();

  // QuicStreamSequencer::StreamInterface implementation.
  QuicStreamId id() const override { return id_; }
  ParsedQuicVersion version() const override;
  // Called by the stream subclass after it has consumed the final incoming
  // data.
  void OnFinRead() override;

  // Called by the subclass or the sequencer to reset the stream from this
  // end.
  void ResetWithError(QuicResetStreamError error) override;
  // Convenience wrapper for the method above.
  // TODO(b/200606367): switch all calls to using QuicResetStreamError
  // interface.
  void Reset(QuicRstStreamErrorCode error);

  // Reset() sends both RESET_STREAM and STOP_SENDING; the two methods below
  // allow to send only one of those.
  void ResetWriteSide(QuicResetStreamError error);
  void SendStopSending(QuicResetStreamError error);

  // Called by the subclass or the sequencer to close the entire connection from
  // this end.
  void OnUnrecoverableError(QuicErrorCode error,
                            const std::string& details) override;
  void OnUnrecoverableError(QuicErrorCode error,
                            QuicIetfTransportErrorCodes ietf_error,
                            const std::string& details) override;

  // Called by the session when a (potentially duplicate) stream frame has been
  // received for this stream.
  virtual void OnStreamFrame(const QuicStreamFrame& frame);

  // Called by the session when the connection becomes writeable to allow the
  // stream to write any pending data.
  virtual void OnCanWrite();

  // Called by the session when the endpoint receives a RST_STREAM from the
  // peer.
  virtual void OnStreamReset(const QuicRstStreamFrame& frame);

  // Called by the session when the endpoint receives or sends a connection
  // close, and should immediately close the stream.
  virtual void OnConnectionClosed(QuicErrorCode error,
                                  ConnectionCloseSource source);

  const QuicStreamPriority& priority() const;

  // Send PRIORITY_UPDATE frame if application protocol supports it.
  virtual void MaybeSendPriorityUpdateFrame() {}

  // Sets |priority_| to priority.  This should only be called before bytes are
  // written to the server.  For a server stream, this is called when a
  // PRIORITY_UPDATE frame is received.  This calls
  // MaybeSendPriorityUpdateFrame(), which for a client stream might send a
  // PRIORITY_UPDATE frame.
  void SetPriority(const QuicStreamPriority& priority);

  // Returns true if this stream is still waiting for acks of sent data.
  // This will return false if all data has been acked, or if the stream
  // is no longer interested in data being acked (which happens when
  // a stream is reset because of an error).
  bool IsWaitingForAcks() const;

  QuicRstStreamErrorCode stream_error() const {
    return stream_error_.internal_code();
  }
  QuicErrorCode connection_error() const { return connection_error_; }

  bool reading_stopped() const {
    return sequencer_.ignore_read_data() || read_side_closed_;
  }
  bool write_side_closed() const { return write_side_closed_; }
  bool read_side_closed() const { return read_side_closed_; }

  bool IsZombie() const {
    return read_side_closed_ && write_side_closed_ && IsWaitingForAcks();
  }

  bool rst_received() const { return rst_received_; }
  bool rst_sent() const { return rst_sent_; }
  bool fin_received() const { return fin_received_; }
  bool fin_sent() const { return fin_sent_; }
  bool fin_outstanding() const { return fin_outstanding_; }
  bool fin_lost() const { return fin_lost_; }

  uint64_t BufferedDataBytes() const;

  uint64_t stream_bytes_read() const { return stream_bytes_read_; }
  uint64_t stream_bytes_written() const;

  size_t busy_counter() const { return busy_counter_; }
  void set_busy_counter(size_t busy_counter) { busy_counter_ = busy_counter; }

  // Adjust the flow control window according to new offset in |frame|.
  virtual void OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame);

  int num_frames_received() const;
  int num_duplicate_frames_received() const;

  // Flow controller related methods.
  bool IsFlowControlBlocked() const;
  QuicStreamOffset highest_received_byte_offset() const;
  void UpdateReceiveWindowSize(QuicStreamOffset size);

  // Called when endpoint receives a frame which could increase the highest
  // offset.
  // Returns true if the highest offset did increase.
  bool MaybeIncreaseHighestReceivedOffset(QuicStreamOffset new_offset);

  // Set the flow controller's send window offset from session config.
  // |was_zero_rtt_rejected| is true if this config is from a rejected IETF QUIC
  // 0-RTT attempt. Closes the connection and returns false if |new_offset| is
  // not valid.
  bool MaybeConfigSendWindowOffset(QuicStreamOffset new_offset,
                                   bool was_zero_rtt_rejected);

  // Returns true if the stream has received either a RST_STREAM or a FIN -
  // either of which gives a definitive number of bytes which the peer has
  // sent. If this is not true on deletion of the stream object, the session
  // must keep track of the stream's byte offset until a definitive final value
  // arrives.
  bool HasReceivedFinalOffset() const { return fin_received_ || rst_received_; }

  // Returns true if the stream has queued data waiting to write.
  bool HasBufferedData() const;

  // Returns the version of QUIC being used for this stream.
  QuicTransportVersion transport_version() const;

  // Returns the crypto handshake protocol that was used on this stream's
  // connection.
  HandshakeProtocol handshake_protocol() const;

  // Sets the sequencer to consume all incoming data itself and not call
  // OnDataAvailable().
  // When the FIN is received, the stream will be notified automatically (via
  // OnFinRead()) (which may happen during the call of StopReading()).
  // TODO(dworley): There should be machinery to send a RST_STREAM/NO_ERROR and
  // stop sending stream-level flow-control updates when this end sends FIN.
  virtual void StopReading();

  // Sends as much of |data| to the connection on the application encryption
  // level as the connection will consume, and then buffers any remaining data
  // in the send buffer. If fin is true: if it is immediately passed on to the
  // session, write_side_closed() becomes true, otherwise fin_buffered_ becomes
  // true.
  void WriteOrBufferData(
      absl::string_view data, bool fin,
      quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
          ack_listener);

  // Sends |data| to connection with specified |level|.
  void WriteOrBufferDataAtLevel(
      absl::string_view data, bool fin, EncryptionLevel level,
      quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
          ack_listener);

  // Adds random padding after the fin is consumed for this stream.
  void AddRandomPaddingAfterFin();

  // Write |data_length| of data starts at |offset| from send buffer.
  bool WriteStreamData(QuicStreamOffset offset, QuicByteCount data_length,
                       QuicDataWriter* writer);

  // Called when data [offset, offset + data_length) is acked. |fin_acked|
  // indicates whether the fin is acked. Returns true and updates
  // |newly_acked_length| if any new stream data (including fin) gets acked.
  virtual bool OnStreamFrameAcked(QuicStreamOffset offset,
                                  QuicByteCount data_length, bool fin_acked,
                                  QuicTime::Delta ack_delay_time,
                                  QuicTime receive_timestamp,
                                  QuicByteCount* newly_acked_length);

  // Called when data [offset, offset + data_length) was retransmitted.
  // |fin_retransmitted| indicates whether fin was retransmitted.
  virtual void OnStreamFrameRetransmitted(QuicStreamOffset offset,
                                          QuicByteCount data_length,
                                          bool fin_retransmitted);

  // Called when data [offset, offset + data_length) is considered as lost.
  // |fin_lost| indicates whether the fin is considered as lost.
  virtual void OnStreamFrameLost(QuicStreamOffset offset,
                                 QuicByteCount data_length, bool fin_lost);

  // Called to retransmit outstanding portion in data [offset, offset +
  // data_length) and |fin| with Transmission |type|.
  // Returns true if all data gets retransmitted.
  virtual bool RetransmitStreamData(QuicStreamOffset offset,
                                    QuicByteCount data_length, bool fin,
                                    TransmissionType type);

  // Sets deadline of this stream to be now + |ttl|, returns true if the setting
  // succeeds.
  bool MaybeSetTtl(QuicTime::Delta ttl);

  // Commits data into the stream write buffer, and potentially sends it over
  // the wire.  This method has all-or-nothing semantics: if the write buffer is
  // not full, all of the memslices in |span| are moved into it; otherwise,
  // nothing happens. If `buffer_unconditionally` is set to true, behaves
  // similar to `WriteOrBufferData()` in terms of buffering.
  QuicConsumedData WriteMemSlices(absl::Span<quiche::QuicheMemSlice> span,
                                  bool fin, bool buffer_uncondtionally = false);
  QuicConsumedData WriteMemSlice(quiche::QuicheMemSlice span, bool fin);

  // Returns true if any stream data is lost (including fin) and needs to be
  // retransmitted.
  virtual bool HasPendingRetransmission() const;

  // Returns true if any portion of data [offset, offset + data_length) is
  // outstanding or fin is outstanding (if |fin| is true). Returns false
  // otherwise.
  bool IsStreamFrameOutstanding(QuicStreamOffset offset,
                                QuicByteCount data_length, bool fin) const;

  StreamType type() const { return type_; }

  // Handle received StopSending frame. Returns true if the processing finishes
  // gracefully.
  virtual bool OnStopSending(QuicResetStreamError error);

  // Returns true if the stream is static.
  bool is_static() const { return is_static_; }

  bool was_draining() const { return was_draining_; }

  QuicTime creation_time() const { return creation_time_; }

  bool fin_buffered() const { return fin_buffered_; }

  // True if buffered data in send buffer is below buffered_data_threshold_.
  bool CanWriteNewData() const;

  // Called immediately after the stream is created from a pending stream,
  // indicating it can start processing data.
  void OnStreamCreatedFromPendingStream();

  void DisableConnectionFlowControlForThisStream() {
    stream_contributes_to_connection_flow_control_ = false;
  }

  // Returns the min of stream level flow control window size and connection
  // level flow control window size.
  QuicByteCount CalculateSendWindowSize() const;

  const QuicTime::Delta pending_duration() const { return pending_duration_; }

 protected:
  // Called when data of [offset, offset + data_length] is buffered in send
  // buffer.
  virtual void OnDataBuffered(
      QuicStreamOffset /*offset*/, QuicByteCount /*data_length*/,
      const quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>&
      /*ack_listener*/) {}

  // Called just before the object is destroyed.
  // The object should not be accessed after OnClose is called.
  // Sends a RST_STREAM with code QUIC_RST_ACKNOWLEDGEMENT if neither a FIN nor
  // a RST_STREAM has been sent.
  virtual void OnClose();

  // True if buffered data in send buffer is still below
  // buffered_data_threshold_ even after writing |length| bytes.
  bool CanWriteNewDataAfterData(QuicByteCount length) const;

  // Called when upper layer can write new data.
  virtual void OnCanWriteNewData() {}

  // Called when |bytes_consumed| bytes has been consumed.
  virtual void OnStreamDataConsumed(QuicByteCount bytes_consumed);

  // Called by the stream sequencer as bytes are consumed from the buffer.
  // If the receive window has dropped below the threshold, then send a
  // WINDOW_UPDATE frame.
  void AddBytesConsumed(QuicByteCount bytes) override;

  // Writes pending retransmissions if any.
  virtual void WritePendingRetransmission();

  // This is called when stream tries to retransmit data after deadline_. Make
  // this virtual so that subclasses can implement their own logics.
  virtual void OnDeadlinePassed();

  // Called to set fin_sent_. This is only used by Google QUIC while body is
  // empty.
  void SetFinSent();

  // Send STOP_SENDING if it hasn't been sent yet.
  void MaybeSendStopSending(QuicResetStreamError error);

  // Send RESET_STREAM if it hasn't been sent yet.
  void MaybeSendRstStream(QuicResetStreamError error);

  // Convenience wrappers for two methods above.
  void MaybeSendRstStream(QuicRstStreamErrorCode error) {
    MaybeSendRstStream(QuicResetStreamError::FromInternal(error));
  }
  void MaybeSendStopSending(QuicRstStreamErrorCode error) {
    MaybeSendStopSending(QuicResetStreamError::FromInternal(error));
  }

  // Close the read side of the stream.  May cause the stream to be closed.
  virtual void CloseReadSide();

  // Close the write side of the socket.  Further writes will fail.
  // Can be called by the subclass or internally.
  // Does not send a FIN.  May cause the stream to be closed.
  virtual void CloseWriteSide();

  void set_rst_received(bool rst_received) { rst_received_ = rst_received; }
  void set_stream_error(QuicResetStreamError error) { stream_error_ = error; }

  StreamDelegateInterface* stream_delegate() { return stream_delegate_; }

  const QuicSession* session() const { return session_; }
  QuicSession* session() { return session_; }

  const QuicStreamSequencer* sequencer() const { return &sequencer_; }
  QuicStreamSequencer* sequencer() { return &sequencer_; }

  const QuicIntervalSet<QuicStreamOffset>& bytes_acked() const;

  const QuicStreamSendBuffer& send_buffer() const { return send_buffer_; }

  QuicStreamSendBuffer& send_buffer() { return send_buffer_; }

  // Called when the write side of the stream is closed, and all of the outgoing
  // data has been acknowledged.  This corresponds to the "Data Recvd" state of
  // RFC 9000.
  virtual void OnWriteSideInDataRecvdState() {}

  // Return the current stream-level flow control send window in bytes.
  std::optional<QuicByteCount> GetSendWindow() const;
  std::optional<QuicByteCount> GetReceiveWindow() const;

 private:
  friend class test::QuicStreamPeer;
  friend class QuicStreamUtils;

  QuicStream(QuicStreamId id, QuicSession* session,
             QuicStreamSequencer sequencer, bool is_static, StreamType type,
             uint64_t stream_bytes_read, bool fin_received,
             std::optional<QuicFlowController> flow_controller,
             QuicFlowController* connection_flow_controller,
             QuicTime::Delta pending_duration);

  // Calls MaybeSendBlocked on the stream's flow controller and the connection
  // level flow controller.  If the stream is flow control blocked by the
  // connection-level flow controller but not by the stream-level flow
  // controller, marks this stream as connection-level write blocked.
  void MaybeSendBlocked();

  // Write buffered data (in send buffer) at |level|.
  void WriteBufferedData(EncryptionLevel level);

  // Called when bytes are sent to the peer.
  void AddBytesSent(QuicByteCount bytes);

  // Returns true if deadline_ has passed.
  bool HasDeadlinePassed() const;

  QuicStreamSequencer sequencer_;
  QuicStreamId id_;
  // Pointer to the owning QuicSession object.
  // TODO(b/136274541): Remove session pointer from streams.
  QuicSession* session_;
  StreamDelegateInterface* stream_delegate_;
  // The priority of the stream, once parsed.
  QuicStreamPriority priority_;
  // Bytes read refers to payload bytes only: they do not include framing,
  // encryption overhead etc.
  uint64_t stream_bytes_read_;

  // Stream error code received from a RstStreamFrame or error code sent by the
  // visitor or sequencer in the RstStreamFrame.
  QuicResetStreamError stream_error_;
  // Connection error code due to which the stream was closed. |stream_error_|
  // is set to |QUIC_STREAM_CONNECTION_ERROR| when this happens and consumers
  // should check |connection_error_|.
  QuicErrorCode connection_error_;

  // True if the read side is closed and further frames should be rejected.
  bool read_side_closed_;
  // True if the write side is closed, and further writes should fail.
  bool write_side_closed_;

  // True if OnWriteSideInDataRecvdState() has already been called.
  bool write_side_data_recvd_state_notified_;

  // True if the subclass has written a FIN with WriteOrBufferData, but it was
  // buffered in queued_data_ rather than being sent to the session.
  bool fin_buffered_;
  // True if a FIN has been sent to the session.
  bool fin_sent_;
  // True if a FIN is waiting to be acked.
  bool fin_outstanding_;
  // True if a FIN is lost.
  bool fin_lost_;

  // True if this stream has received (and the sequencer has accepted) a
  // StreamFrame with the FIN set.
  bool fin_received_;

  // True if an RST_STREAM has been sent to the session.
  // In combination with fin_sent_, used to ensure that a FIN and/or a
  // RST_STREAM is always sent to terminate the stream.
  bool rst_sent_;

  // True if this stream has received a RST_STREAM frame.
  bool rst_received_;

  // True if the stream has sent STOP_SENDING to the session.
  bool stop_sending_sent_;

  std::optional<QuicFlowController> flow_controller_;

  // The connection level flow controller. Not owned.
  QuicFlowController* connection_flow_controller_;

  // Special streams, such as the crypto and headers streams, do not respect
  // connection level flow control limits (but are stream level flow control
  // limited).
  bool stream_contributes_to_connection_flow_control_;

  // A counter incremented when OnCanWrite() is called and no progress is made.
  // For debugging only.
  size_t busy_counter_;

  // Indicates whether paddings will be added after the fin is consumed for this
  // stream.
  bool add_random_padding_after_fin_;

  // Send buffer of this stream. Send buffer is cleaned up when data gets acked
  // or discarded.
  QuicStreamSendBuffer send_buffer_;

  // Latched value of quic_buffered_data_threshold.
  const QuicByteCount buffered_data_threshold_;

  // If true, then this stream has precedence over other streams for write
  // scheduling.
  const bool is_static_;

  // If initialized, reset this stream at this deadline.
  QuicTime deadline_;

  // True if this stream has entered draining state.
  bool was_draining_;

  // Indicates whether this stream is bidirectional, read unidirectional or
  // write unidirectional.
  const StreamType type_;

  // Creation time of this stream, as reported by the QuicClock.
  const QuicTime creation_time_;

  // The duration when the data for this stream was stored in a PendingStream
  // before being moved to this QuicStream.
  const QuicTime::Delta pending_duration_;

  Perspective perspective_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_STREAM_H_
