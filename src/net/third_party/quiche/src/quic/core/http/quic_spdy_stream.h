// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// The base class for streams which deliver data to/from an application.
// In each direction, the data on such a stream first contains compressed
// headers then body data.

#ifndef QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_STREAM_H_
#define QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_STREAM_H_

#include <sys/types.h>

#include <cstddef>
#include <list>
#include <string>

#include "net/third_party/quiche/src/quic/core/http/http_decoder.h"
#include "net/third_party/quiche/src/quic/core/http/http_encoder.h"
#include "net/third_party/quiche/src/quic/core/http/quic_header_list.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_stream_body_manager.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoded_headers_accumulator.h"
#include "net/third_party/quiche/src/quic/core/quic_packets.h"
#include "net/third_party/quiche/src/quic/core/quic_stream.h"
#include "net/third_party/quiche/src/quic/core/quic_stream_sequencer.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/spdy/core/spdy_framer.h"

namespace quic {

namespace test {
class QuicSpdyStreamPeer;
class QuicStreamPeer;
}  // namespace test

class QuicSpdySession;

// A QUIC stream that can send and receive HTTP2 (SPDY) headers.
class QUIC_EXPORT_PRIVATE QuicSpdyStream
    : public QuicStream,
      public QpackDecodedHeadersAccumulator::Visitor {
 public:
  // Visitor receives callbacks from the stream.
  class QUIC_EXPORT_PRIVATE Visitor {
   public:
    Visitor() {}
    Visitor(const Visitor&) = delete;
    Visitor& operator=(const Visitor&) = delete;

    // Called when the stream is closed.
    virtual void OnClose(QuicSpdyStream* stream) = 0;

    // Allows subclasses to override and do work.
    virtual void OnPromiseHeadersComplete(QuicStreamId /*promised_id*/,
                                          size_t /*frame_len*/) {}

   protected:
    virtual ~Visitor() {}
  };

  QuicSpdyStream(QuicStreamId id,
                 QuicSpdySession* spdy_session,
                 StreamType type);
  QuicSpdyStream(PendingStream* pending,
                 QuicSpdySession* spdy_session,
                 StreamType type);
  QuicSpdyStream(const QuicSpdyStream&) = delete;
  QuicSpdyStream& operator=(const QuicSpdyStream&) = delete;
  ~QuicSpdyStream() override;

  // QuicStream implementation
  void OnClose() override;

  // Override to maybe close the write side after writing.
  void OnCanWrite() override;

  // Called by the session when headers with a priority have been received
  // for this stream.  This method will only be called for server streams.
  virtual void OnStreamHeadersPriority(
      const spdy::SpdyStreamPrecedence& precedence);

  // Called by the session when decompressed headers have been completely
  // delivered to this stream.  If |fin| is true, then this stream
  // should be closed; no more data will be sent by the peer.
  virtual void OnStreamHeaderList(bool fin,
                                  size_t frame_len,
                                  const QuicHeaderList& header_list);

  // Called by the session when decompressed push promise headers have
  // been completely delivered to this stream.
  virtual void OnPromiseHeaderList(QuicStreamId promised_id,
                                   size_t frame_len,
                                   const QuicHeaderList& header_list);

  // Called by the session when a PRIORITY frame has been been received for this
  // stream. This method will only be called for server streams.
  void OnPriorityFrame(const spdy::SpdyStreamPrecedence& precedence);

  // Override the base class to not discard response when receiving
  // QUIC_STREAM_NO_ERROR.
  void OnStreamReset(const QuicRstStreamFrame& frame) override;

  void Reset(QuicRstStreamErrorCode error) override;

  // Called by the sequencer when new data is available. Decodes the data and
  // calls OnBodyAvailable() to pass to the upper layer.
  void OnDataAvailable() override;

  // Called in OnDataAvailable() after it finishes the decoding job.
  virtual void OnBodyAvailable() = 0;

  // Writes the headers contained in |header_block| on the dedicated headers
  // stream or on this stream, depending on VersionUsesHttp3().  Returns the
  // number of bytes sent, including data sent on the encoder stream when using
  // QPACK.
  virtual size_t WriteHeaders(
      spdy::SpdyHeaderBlock header_block,
      bool fin,
      QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener);

  // Sends |data| to the peer, or buffers if it can't be sent immediately.
  void WriteOrBufferBody(quiche::QuicheStringPiece data, bool fin);

  // Writes the trailers contained in |trailer_block| on the dedicated headers
  // stream or on this stream, depending on VersionUsesHttp3().  Trailers will
  // always have the FIN flag set.  Returns the number of bytes sent, including
  // data sent on the encoder stream when using QPACK.
  virtual size_t WriteTrailers(
      spdy::SpdyHeaderBlock trailer_block,
      QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener);

  // Serializes |frame| and writes the encoded push promise data.
  void WritePushPromise(const PushPromiseFrame& frame);

  // Override to report newly acked bytes via ack_listener_.
  bool OnStreamFrameAcked(QuicStreamOffset offset,
                          QuicByteCount data_length,
                          bool fin_acked,
                          QuicTime::Delta ack_delay_time,
                          QuicTime receive_timestamp,
                          QuicByteCount* newly_acked_length) override;

  // Override to report bytes retransmitted via ack_listener_.
  void OnStreamFrameRetransmitted(QuicStreamOffset offset,
                                  QuicByteCount data_length,
                                  bool fin_retransmitted) override;

  // Does the same thing as WriteOrBufferBody except this method takes iovec
  // as the data input. Right now it only calls WritevData.
  QuicConsumedData WritevBody(const struct iovec* iov, int count, bool fin);

  // Does the same thing as WriteOrBufferBody except this method takes
  // memslicespan as the data input. Right now it only calls WriteMemSlices.
  QuicConsumedData WriteBodySlices(QuicMemSliceSpan slices, bool fin);

  // Marks the trailers as consumed. This applies to the case where this object
  // receives headers and trailers as QuicHeaderLists via calls to
  // OnStreamHeaderList(). Trailer data will be consumed from the sequencer only
  // once all body data has been consumed.
  void MarkTrailersConsumed();

  // Clears |header_list_|.
  void ConsumeHeaderList();

  void SetUnblocked() { sequencer()->SetUnblocked(); }

  // This block of functions wraps the sequencer's functions of the same
  // name.  These methods return uncompressed data until that has
  // been fully processed.  Then they simply delegate to the sequencer.
  virtual size_t Readv(const struct iovec* iov, size_t iov_len);
  virtual int GetReadableRegions(iovec* iov, size_t iov_len) const;
  void MarkConsumed(size_t num_bytes);

  // Returns true if header contains a valid 3-digit status and parse the status
  // code to |status_code|.
  static bool ParseHeaderStatusCode(const spdy::SpdyHeaderBlock& header,
                                    int* status_code);

  // Returns true when all data from the peer has been read and consumed,
  // including the fin.
  bool IsDoneReading() const;
  bool HasBytesToRead() const;

  void set_visitor(Visitor* visitor) { visitor_ = visitor; }

  bool headers_decompressed() const { return headers_decompressed_; }

  // Returns total amount of body bytes that have been read.
  uint64_t total_body_bytes_read() const;

  const QuicHeaderList& header_list() const { return header_list_; }

  bool trailers_decompressed() const { return trailers_decompressed_; }

  // Returns whatever trailers have been received for this stream.
  const spdy::SpdyHeaderBlock& received_trailers() const {
    return received_trailers_;
  }

  // Returns true if headers have been fully read and consumed.
  bool FinishedReadingHeaders() const;

  // Returns true if FIN has been received and either trailers have been fully
  // read and consumed or there are no trailers.
  bool FinishedReadingTrailers() const;

  // Called when owning session is getting deleted to avoid subsequent
  // use of the spdy_session_ member.
  void ClearSession();

  // Returns true if the sequencer has delivered the FIN, and no more body bytes
  // will be available.
  bool IsClosed() { return sequencer()->IsClosed(); }

  // QpackDecodedHeadersAccumulator::Visitor implementation.
  void OnHeadersDecoded(QuicHeaderList headers,
                        bool header_list_size_limit_exceeded) override;
  void OnHeaderDecodingError(quiche::QuicheStringPiece error_message) override;

  QuicSpdySession* spdy_session() const { return spdy_session_; }

  // Send PRIORITY_UPDATE frame and update |last_sent_urgency_| if
  // |last_sent_urgency_| is different from current priority.
  void MaybeSendPriorityUpdateFrame() override;

 protected:
  // Called when the received headers are too large. By default this will
  // reset the stream.
  virtual void OnHeadersTooLarge();

  virtual void OnInitialHeadersComplete(bool fin,
                                        size_t frame_len,
                                        const QuicHeaderList& header_list);
  virtual void OnTrailingHeadersComplete(bool fin,
                                         size_t frame_len,
                                         const QuicHeaderList& header_list);
  virtual size_t WriteHeadersImpl(
      spdy::SpdyHeaderBlock header_block,
      bool fin,
      QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener);

  Visitor* visitor() { return visitor_; }

  void set_headers_decompressed(bool val) { headers_decompressed_ = val; }

  void set_ack_listener(
      QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
    ack_listener_ = std::move(ack_listener);
  }

 private:
  friend class test::QuicSpdyStreamPeer;
  friend class test::QuicStreamPeer;
  friend class QuicStreamUtils;
  class HttpDecoderVisitor;

  // Called by HttpDecoderVisitor.
  bool OnDataFrameStart(QuicByteCount header_length,
                        QuicByteCount payload_length);
  bool OnDataFramePayload(quiche::QuicheStringPiece payload);
  bool OnDataFrameEnd();
  bool OnHeadersFrameStart(QuicByteCount header_length,
                           QuicByteCount payload_length);
  bool OnHeadersFramePayload(quiche::QuicheStringPiece payload);
  bool OnHeadersFrameEnd();
  bool OnPushPromiseFrameStart(QuicByteCount header_length);
  bool OnPushPromiseFramePushId(PushId push_id,
                                QuicByteCount push_id_length,
                                QuicByteCount header_block_length);
  bool OnPushPromiseFramePayload(quiche::QuicheStringPiece payload);
  bool OnPushPromiseFrameEnd();
  bool OnUnknownFrameStart(uint64_t frame_type,
                           QuicByteCount header_length,
                           QuicByteCount payload_length);
  bool OnUnknownFramePayload(quiche::QuicheStringPiece payload);
  bool OnUnknownFrameEnd();

  // Given the interval marked by [|offset|, |offset| + |data_length|), return
  // the number of frame header bytes contained in it.
  QuicByteCount GetNumFrameHeadersInInterval(QuicStreamOffset offset,
                                             QuicByteCount data_length) const;

  QuicSpdySession* spdy_session_;

  bool on_body_available_called_because_sequencer_is_closed_;

  Visitor* visitor_;

  // True if read side processing is blocked while waiting for callback from
  // QPACK decoder.
  bool blocked_on_decoding_headers_;
  // True if the headers have been completely decompressed.
  bool headers_decompressed_;
  // True if uncompressed headers or trailers exceed maximum allowed size
  // advertised to peer via SETTINGS_MAX_HEADER_LIST_SIZE.
  bool header_list_size_limit_exceeded_;
  // Contains a copy of the decompressed header (name, value) pairs until they
  // are consumed via Readv.
  QuicHeaderList header_list_;
  // Length of HEADERS frame payload.
  QuicByteCount headers_payload_length_;
  // Length of TRAILERS frame payload.
  QuicByteCount trailers_payload_length_;

  // True if the trailers have been completely decompressed.
  bool trailers_decompressed_;
  // True if the trailers have been consumed.
  bool trailers_consumed_;

  // The parsed trailers received from the peer.
  spdy::SpdyHeaderBlock received_trailers_;

  // Headers accumulator for decoding HEADERS frame payload.
  std::unique_ptr<QpackDecodedHeadersAccumulator>
      qpack_decoded_headers_accumulator_;
  // Visitor of the HttpDecoder.
  std::unique_ptr<HttpDecoderVisitor> http_decoder_visitor_;
  // HttpDecoder for processing raw incoming stream frames.
  HttpDecoder decoder_;
  // Object that manages references to DATA frame payload fragments buffered by
  // the sequencer and calculates how much data should be marked consumed with
  // the sequencer each time new stream data is processed.
  QuicSpdyStreamBodyManager body_manager_;

  // Sequencer offset keeping track of how much data HttpDecoder has processed.
  // Initial value is zero for fresh streams, or sequencer()->NumBytesConsumed()
  // at time of construction if a PendingStream is converted to account for the
  // length of the unidirectional stream type at the beginning of the stream.
  QuicStreamOffset sequencer_offset_;

  // True when inside an HttpDecoder::ProcessInput() call.
  // Used for detecting reentrancy.
  bool is_decoder_processing_input_;

  // Ack listener of this stream, and it is notified when any of written bytes
  // are acked or retransmitted.
  QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener_;

  // Offset of unacked frame headers.
  QuicIntervalSet<QuicStreamOffset> unacked_frame_headers_offsets_;

  // Urgency value sent in the last PRIORITY_UPDATE frame, or default urgency
  // defined by the spec if no PRIORITY_UPDATE frame has been sent.
  int last_sent_urgency_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_STREAM_H_
