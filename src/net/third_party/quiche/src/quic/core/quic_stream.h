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
#include <string>

#include "net/third_party/quiche/src/quic/core/quic_flow_controller.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_stream_send_buffer.h"
#include "net/third_party/quiche/src/quic/core/quic_stream_sequencer.h"
#include "net/third_party/quiche/src/quic/core/quic_types.h"
#include "net/third_party/quiche/src/quic/core/session_notifier_interface.h"
#include "net/third_party/quiche/src/quic/core/stream_delegate_interface.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice_span.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_reference_counted.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_optional.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"

namespace quic {

namespace test {
class QuicStreamPeer;
}  // namespace test

class QuicSession;
class QuicStream;

// Buffers frames for a stream until the first byte of that frame arrives.
class QUIC_EXPORT_PRIVATE PendingStream
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
  void Reset(QuicRstStreamErrorCode error) override;
  void OnUnrecoverableError(QuicErrorCode error,
                            const std::string& details) override;
  QuicStreamId id() const override;

  // Buffers the contents of |frame|. Frame must have a non-zero offset.
  // If the data violates flow control, the connection will be closed.
  void OnStreamFrame(const QuicStreamFrame& frame);

  // Stores the final byte offset from |frame|.
  // If the final offset violates flow control, the connection will be closed.
  void OnRstStreamFrame(const QuicRstStreamFrame& frame);

  // Returns the number of bytes read on this stream.
  uint64_t stream_bytes_read() { return stream_bytes_read_; }

  const QuicStreamSequencer* sequencer() const { return &sequencer_; }

  void MarkConsumed(size_t num_bytes);

  // Tells the sequencer to ignore all incoming data itself and not call
  // OnDataAvailable().
  void StopReading();

 private:
  friend class QuicStream;

  bool MaybeIncreaseHighestReceivedOffset(QuicStreamOffset new_offset);

  // ID of this stream.
  QuicStreamId id_;

  // Session which owns this.
  // TODO(b/136274541): Remove session pointer from streams.
  QuicSession* session_;
  StreamDelegateInterface* stream_delegate_;

  // Bytes read refers to payload bytes only: they do not include framing,
  // encryption overhead etc.
  uint64_t stream_bytes_read_;

  // True if a frame containing a fin has been received.
  bool fin_received_;

  // Connection-level flow controller. Owned by the session.
  QuicFlowController* connection_flow_controller_;
  // Stream-level flow controller.
  QuicFlowController flow_controller_;
  // Stores the buffered frames.
  QuicStreamSequencer sequencer_;
};

class QUIC_EXPORT_PRIVATE QuicStream
    : public QuicStreamSequencer::StreamInterface {
 public:
  // This is somewhat arbitrary.  It's possible, but unlikely, we will either
  // fail to set a priority client-side, or cancel a stream before stripping the
  // priority from the wire server-side.  In either case, start out with a
  // priority in the middle in case of Google QUIC.
  static const spdy::SpdyPriority kDefaultPriority = 3;
  static_assert(kDefaultPriority ==
                    (spdy::kV3LowestPriority + spdy::kV3HighestPriority) / 2,
                "Unexpected value of kDefaultPriority");
  // On the other hand, when using IETF QUIC, use the default value defined by
  // the priority extension at
  // https://httpwg.org/http-extensions/draft-ietf-httpbis-priority.html#default.
  static const int kDefaultUrgency = 1;

  // Creates a new stream with stream_id |id| associated with |session|. If
  // |is_static| is true, then the stream will be given precedence
  // over other streams when determing what streams should write next.
  // |type| indicates whether the stream is bidirectional, read unidirectional
  // or write unidirectional.
  // TODO(fayang): Remove |type| when IETF stream ID numbering fully kicks in.
  QuicStream(QuicStreamId id,
             QuicSession* session,
             bool is_static,
             StreamType type);
  QuicStream(PendingStream* pending, StreamType type, bool is_static);
  QuicStream(const QuicStream&) = delete;
  QuicStream& operator=(const QuicStream&) = delete;

  virtual ~QuicStream();

  // QuicStreamSequencer::StreamInterface implementation.
  QuicStreamId id() const override { return id_; }
  // Called by the stream subclass after it has consumed the final incoming
  // data.
  void OnFinRead() override;

  // Called by the subclass or the sequencer to reset the stream from this
  // end.
  void Reset(QuicRstStreamErrorCode error) override;

  // Called by the subclass or the sequencer to close the entire connection from
  // this end.
  void OnUnrecoverableError(QuicErrorCode error,
                            const std::string& details) override;

  // Called by the session when a (potentially duplicate) stream frame has been
  // received for this stream.
  virtual void OnStreamFrame(const QuicStreamFrame& frame);

  // Called by the session when the connection becomes writeable to allow the
  // stream to write any pending data.
  virtual void OnCanWrite();

  // Called by the session just before the object is destroyed.
  // The object should not be accessed after OnClose is called.
  // Sends a RST_STREAM with code QUIC_RST_ACKNOWLEDGEMENT if neither a FIN nor
  // a RST_STREAM has been sent.
  virtual void OnClose();

  // Called by the session when the endpoint receives a RST_STREAM from the
  // peer.
  virtual void OnStreamReset(const QuicRstStreamFrame& frame);

  // Called by the session when the endpoint receives or sends a connection
  // close, and should immediately close the stream.
  virtual void OnConnectionClosed(QuicErrorCode error,
                                  ConnectionCloseSource source);

  const spdy::SpdyStreamPrecedence& precedence() const;

  // Send PRIORITY_UPDATE frame if application protocol supports it.
  virtual void MaybeSendPriorityUpdateFrame() {}

  // Sets |priority_| to priority.  This should only be called before bytes are
  // written to the server.  For a server stream, this is called when a
  // PRIORITY_UPDATE frame is received.  This calls
  // MaybeSendPriorityUpdateFrame(), which for a client stream might send a
  // PRIORITY_UPDATE frame.
  void SetPriority(const spdy::SpdyStreamPrecedence& precedence);

  // Returns true if this stream is still waiting for acks of sent data.
  // This will return false if all data has been acked, or if the stream
  // is no longer interested in data being acked (which happens when
  // a stream is reset because of an error).
  bool IsWaitingForAcks() const;

  // Number of bytes available to read.
  size_t ReadableBytes() const;

  QuicRstStreamErrorCode stream_error() const { return stream_error_; }
  QuicErrorCode connection_error() const { return connection_error_; }

  bool reading_stopped() const {
    return sequencer_.ignore_read_data() || read_side_closed_;
  }
  bool write_side_closed() const { return write_side_closed_; }

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

  void set_fin_sent(bool fin_sent) { fin_sent_ = fin_sent; }
  void set_fin_received(bool fin_received) { fin_received_ = fin_received; }
  void set_rst_sent(bool rst_sent) { rst_sent_ = rst_sent; }

  void set_rst_received(bool rst_received) { rst_received_ = rst_received; }
  void set_stream_error(QuicRstStreamErrorCode error) { stream_error_ = error; }

  // Adjust the flow control window according to new offset in |frame|.
  virtual void OnWindowUpdateFrame(const QuicWindowUpdateFrame& frame);

  int num_frames_received() const;
  int num_duplicate_frames_received() const;

  QuicFlowController* flow_controller() { return &*flow_controller_; }

  // Called when endpoint receives a frame which could increase the highest
  // offset.
  // Returns true if the highest offset did increase.
  bool MaybeIncreaseHighestReceivedOffset(QuicStreamOffset new_offset);

  // Updates the flow controller's send window offset and calls OnCanWrite if
  // it was blocked before.
  void UpdateSendWindowOffset(QuicStreamOffset new_offset);

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

  // Sends as much of 'data' to the connection as the connection will consume,
  // and then buffers any remaining data in queued_data_.
  // If fin is true: if it is immediately passed on to the session,
  // write_side_closed() becomes true, otherwise fin_buffered_ becomes true.
  void WriteOrBufferData(
      quiche::QuicheStringPiece data,
      bool fin,
      QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener);

  // Adds random padding after the fin is consumed for this stream.
  void AddRandomPaddingAfterFin();

  // Write |data_length| of data starts at |offset| from send buffer.
  bool WriteStreamData(QuicStreamOffset offset,
                       QuicByteCount data_length,
                       QuicDataWriter* writer);

  // Called when data [offset, offset + data_length) is acked. |fin_acked|
  // indicates whether the fin is acked. Returns true and updates
  // |newly_acked_length| if any new stream data (including fin) gets acked.
  virtual bool OnStreamFrameAcked(QuicStreamOffset offset,
                                  QuicByteCount data_length,
                                  bool fin_acked,
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
                                 QuicByteCount data_length,
                                 bool fin_lost);

  // Called to retransmit outstanding portion in data [offset, offset +
  // data_length) and |fin| with Transmission |type|.
  // Returns true if all data gets retransmitted.
  virtual bool RetransmitStreamData(QuicStreamOffset offset,
                                    QuicByteCount data_length,
                                    bool fin,
                                    TransmissionType type);

  // Sets deadline of this stream to be now + |ttl|, returns true if the setting
  // succeeds.
  bool MaybeSetTtl(QuicTime::Delta ttl);

  // Same as WritevData except data is provided in reference counted memory so
  // that data copy is avoided.
  QuicConsumedData WriteMemSlices(QuicMemSliceSpan span, bool fin);

  // Returns true if any stream data is lost (including fin) and needs to be
  // retransmitted.
  virtual bool HasPendingRetransmission() const;

  // Returns true if any portion of data [offset, offset + data_length) is
  // outstanding or fin is outstanding (if |fin| is true). Returns false
  // otherwise.
  bool IsStreamFrameOutstanding(QuicStreamOffset offset,
                                QuicByteCount data_length,
                                bool fin) const;

  StreamType type() const { return type_; }

  // Creates and sends a STOP_SENDING frame.  This can be called regardless of
  // the version that has been negotiated.  If not IETF QUIC/Version 99 then the
  // method is a noop, relieving the application of the necessity of
  // understanding the connection's QUIC version and knowing whether it can call
  // this method or not.
  void SendStopSending(uint16_t code);

  // Handle received StopSending frame. Returns true if the processing finishes
  // gracefully.
  virtual bool OnStopSending(uint16_t code);

  // Close the write side of the socket.  Further writes will fail.
  // Can be called by the subclass or internally.
  // Does not send a FIN.  May cause the stream to be closed.
  virtual void CloseWriteSide();

  // Returns true if the stream is static.
  bool is_static() const { return is_static_; }

  static spdy::SpdyStreamPrecedence CalculateDefaultPriority(
      const QuicSession* session);

 protected:
  // Close the read side of the socket.  May cause the stream to be closed.
  // Subclasses and consumers should use StopReading to terminate reading early
  // if expecting a FIN. Can be used directly by subclasses if not expecting a
  // FIN.
  void CloseReadSide();

  // Called when data of [offset, offset + data_length] is buffered in send
  // buffer.
  virtual void OnDataBuffered(
      QuicStreamOffset /*offset*/,
      QuicByteCount /*data_length*/,
      const QuicReferenceCountedPointer<QuicAckListenerInterface>&
      /*ack_listener*/) {}

  // True if buffered data in send buffer is below buffered_data_threshold_.
  bool CanWriteNewData() const;

  // True if buffered data in send buffer is still below
  // buffered_data_threshold_ even after writing |length| bytes.
  bool CanWriteNewDataAfterData(QuicByteCount length) const;

  // Called when upper layer can write new data.
  virtual void OnCanWriteNewData() {}

  // Called when |bytes_consumed| bytes has been consumed.
  virtual void OnStreamDataConsumed(size_t bytes_consumed);

  // Called by the stream sequencer as bytes are consumed from the buffer.
  // If the receive window has dropped below the threshold, then send a
  // WINDOW_UPDATE frame.
  void AddBytesConsumed(QuicByteCount bytes) override;

  // Writes pending retransmissions if any.
  virtual void WritePendingRetransmission();

  // This is called when stream tries to retransmit data after deadline_. Make
  // this virtual so that subclasses can implement their own logics.
  virtual void OnDeadlinePassed();

  StreamDelegateInterface* stream_delegate() { return stream_delegate_; }

  bool fin_buffered() const { return fin_buffered_; }

  const QuicSession* session() const { return session_; }
  QuicSession* session() { return session_; }

  const QuicStreamSequencer* sequencer() const { return &sequencer_; }
  QuicStreamSequencer* sequencer() { return &sequencer_; }

  void DisableConnectionFlowControlForThisStream() {
    stream_contributes_to_connection_flow_control_ = false;
  }

  const QuicIntervalSet<QuicStreamOffset>& bytes_acked() const;

  const QuicStreamSendBuffer& send_buffer() const { return send_buffer_; }

  QuicStreamSendBuffer& send_buffer() { return send_buffer_; }

 private:
  friend class test::QuicStreamPeer;
  friend class QuicStreamUtils;

  QuicStream(QuicStreamId id,
             QuicSession* session,
             QuicStreamSequencer sequencer,
             bool is_static,
             StreamType type,
             uint64_t stream_bytes_read,
             bool fin_received,
             quiche::QuicheOptional<QuicFlowController> flow_controller,
             QuicFlowController* connection_flow_controller);

  // Calls MaybeSendBlocked on the stream's flow controller and the connection
  // level flow controller.  If the stream is flow control blocked by the
  // connection-level flow controller but not by the stream-level flow
  // controller, marks this stream as connection-level write blocked.
  void MaybeSendBlocked();

  // Write buffered data in send buffer. TODO(fayang): Consider combine
  // WriteOrBufferData, Writev and WriteBufferedData.
  void WriteBufferedData();

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
  // The precedence of the stream, once parsed.
  spdy::SpdyStreamPrecedence precedence_;
  // Bytes read refers to payload bytes only: they do not include framing,
  // encryption overhead etc.
  uint64_t stream_bytes_read_;

  // Stream error code received from a RstStreamFrame or error code sent by the
  // visitor or sequencer in the RstStreamFrame.
  QuicRstStreamErrorCode stream_error_;
  // Connection error code due to which the stream was closed. |stream_error_|
  // is set to |QUIC_STREAM_CONNECTION_ERROR| when this happens and consumers
  // should check |connection_error_|.
  QuicErrorCode connection_error_;

  // True if the read side is closed and further frames should be rejected.
  bool read_side_closed_;
  // True if the write side is closed, and further writes should fail.
  bool write_side_closed_;

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

  quiche::QuicheOptional<QuicFlowController> flow_controller_;

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

  // Indicates whether this stream is bidirectional, read unidirectional or
  // write unidirectional.
  const StreamType type_;

  Perspective perspective_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_STREAM_H_
