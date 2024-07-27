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
#include <memory>
#include <optional>
#include <string>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/quic/core/http/http_decoder.h"
#include "quiche/quic/core/http/http_encoder.h"
#include "quiche/quic/core/http/metadata_decoder.h"
#include "quiche/quic/core/http/quic_header_list.h"
#include "quiche/quic/core/http/quic_spdy_stream_body_manager.h"
#include "quiche/quic/core/http/web_transport_stream_adapter.h"
#include "quiche/quic/core/qpack/qpack_decoded_headers_accumulator.h"
#include "quiche/quic/core/quic_error_codes.h"
#include "quiche/quic/core/quic_packets.h"
#include "quiche/quic/core/quic_session.h"
#include "quiche/quic/core/quic_stream.h"
#include "quiche/quic/core/quic_stream_priority.h"
#include "quiche/quic/core/quic_stream_sequencer.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/web_transport_interface.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/capsule.h"
#include "quiche/common/platform/api/quiche_mem_slice.h"
#include "quiche/spdy/core/http2_header_block.h"
#include "quiche/spdy/core/spdy_framer.h"

namespace quic {

namespace test {
class QuicSpdyStreamPeer;
class QuicStreamPeer;
}  // namespace test

class QuicSpdySession;
class WebTransportHttp3;

// A QUIC stream that can send and receive HTTP2 (SPDY) headers.
class QUICHE_EXPORT QuicSpdyStream
    : public QuicStream,
      public quiche::CapsuleParser::Visitor,
      public QpackDecodedHeadersAccumulator::Visitor {
 public:
  // Visitor receives callbacks from the stream.
  class QUICHE_EXPORT Visitor {
   public:
    Visitor() {}
    Visitor(const Visitor&) = delete;
    Visitor& operator=(const Visitor&) = delete;

    // Called when the stream is closed.
    virtual void OnClose(QuicSpdyStream* stream) = 0;

   protected:
    virtual ~Visitor() {}
  };

  // Class which receives HTTP/3 METADATA.
  class QUICHE_EXPORT MetadataVisitor {
   public:
    virtual ~MetadataVisitor() = default;

    // Called when HTTP/3 METADATA has been received and parsed.
    virtual void OnMetadataComplete(size_t frame_len,
                                    const QuicHeaderList& header_list) = 0;
  };

  class QUICHE_EXPORT Http3DatagramVisitor {
   public:
    virtual ~Http3DatagramVisitor() {}

    // Called when an HTTP/3 datagram is received. |payload| does not contain
    // the stream ID.
    virtual void OnHttp3Datagram(QuicStreamId stream_id,
                                 absl::string_view payload) = 0;

    // Called when a Capsule with an unknown type is received.
    virtual void OnUnknownCapsule(QuicStreamId stream_id,
                                  const quiche::UnknownCapsule& capsule) = 0;
  };

  class QUICHE_EXPORT ConnectIpVisitor {
   public:
    virtual ~ConnectIpVisitor() {}

    virtual bool OnAddressAssignCapsule(
        const quiche::AddressAssignCapsule& capsule) = 0;
    virtual bool OnAddressRequestCapsule(
        const quiche::AddressRequestCapsule& capsule) = 0;
    virtual bool OnRouteAdvertisementCapsule(
        const quiche::RouteAdvertisementCapsule& capsule) = 0;
    virtual void OnHeadersWritten() = 0;
  };

  QuicSpdyStream(QuicStreamId id, QuicSpdySession* spdy_session,
                 StreamType type);
  QuicSpdyStream(PendingStream* pending, QuicSpdySession* spdy_session);
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
  virtual void OnStreamHeaderList(bool fin, size_t frame_len,
                                  const QuicHeaderList& header_list);

  // Called by the session when a PRIORITY frame has been been received for this
  // stream. This method will only be called for server streams.
  void OnPriorityFrame(const spdy::SpdyStreamPrecedence& precedence);

  // Override the base class to not discard response when receiving
  // QUIC_STREAM_NO_ERROR.
  void OnStreamReset(const QuicRstStreamFrame& frame) override;
  void ResetWithError(QuicResetStreamError error) override;
  bool OnStopSending(QuicResetStreamError error) override;

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
      spdy::Http2HeaderBlock header_block, bool fin,
      quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
          ack_listener);

  // Sends |data| to the peer, or buffers if it can't be sent immediately.
  virtual void WriteOrBufferBody(absl::string_view data, bool fin);

  // Writes the trailers contained in |trailer_block| on the dedicated headers
  // stream or on this stream, depending on VersionUsesHttp3().  Trailers will
  // always have the FIN flag set.  Returns the number of bytes sent, including
  // data sent on the encoder stream when using QPACK.
  virtual size_t WriteTrailers(
      spdy::Http2HeaderBlock trailer_block,
      quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
          ack_listener);

  // Override to report newly acked bytes via ack_listener_.
  bool OnStreamFrameAcked(QuicStreamOffset offset, QuicByteCount data_length,
                          bool fin_acked, QuicTime::Delta ack_delay_time,
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
  QuicConsumedData WriteBodySlices(absl::Span<quiche::QuicheMemSlice> slices,
                                   bool fin);

  // Marks the trailers as consumed. This applies to the case where this object
  // receives headers and trailers as QuicHeaderLists via calls to
  // OnStreamHeaderList(). Trailer data will be consumed from the sequencer only
  // once all body data has been consumed.
  void MarkTrailersConsumed();

  // Clears |header_list_|.
  void ConsumeHeaderList();

  // This block of functions wraps the sequencer's functions of the same
  // name.  These methods return uncompressed data until that has
  // been fully processed.  Then they simply delegate to the sequencer.
  virtual size_t Readv(const struct iovec* iov, size_t iov_len);
  virtual int GetReadableRegions(iovec* iov, size_t iov_len) const;
  void MarkConsumed(size_t num_bytes);

  // Returns true if header contains a valid 3-digit status and parse the status
  // code to |status_code|.
  static bool ParseHeaderStatusCode(const spdy::Http2HeaderBlock& header,
                                    int* status_code);
  // Returns true if status_value (associated with :status) contains a valid
  // 3-digit status and parse the status code to |status_code|.
  static bool ParseHeaderStatusCode(absl::string_view status_value,
                                    int* status_code);

  // Returns true when headers, data and trailers all are read.
  bool IsDoneReading() const;
  // For IETF QUIC, bytes-to-read/readable-bytes only concern body (not headers
  // or trailers). For gQUIC, they refer to all the bytes in the sequencer.
  bool HasBytesToRead() const;
  QuicByteCount ReadableBytes() const;

  void set_visitor(Visitor* visitor) { visitor_ = visitor; }

  bool headers_decompressed() const { return headers_decompressed_; }

  // Returns total amount of body bytes that have been read.
  uint64_t total_body_bytes_read() const;

  const QuicHeaderList& header_list() const { return header_list_; }

  bool trailers_decompressed() const { return trailers_decompressed_; }

  // Returns whatever trailers have been received for this stream.
  const spdy::Http2HeaderBlock& received_trailers() const {
    return received_trailers_;
  }

  // Returns true if headers have been fully read and consumed.
  bool FinishedReadingHeaders() const;

  // Returns true if FIN has been received and either trailers have been fully
  // read and consumed or there are no trailers.
  bool FinishedReadingTrailers() const;

  // Returns true if the sequencer has delivered the FIN, and no more body bytes
  // will be available.
  bool IsSequencerClosed() { return sequencer()->IsClosed(); }

  // QpackDecodedHeadersAccumulator::Visitor implementation.
  void OnHeadersDecoded(QuicHeaderList headers,
                        bool header_list_size_limit_exceeded) override;
  void OnHeaderDecodingError(QuicErrorCode error_code,
                             absl::string_view error_message) override;

  QuicSpdySession* spdy_session() const { return spdy_session_; }

  // Send PRIORITY_UPDATE frame and update |last_sent_priority_| if
  // |last_sent_priority_| is different from current priority.
  void MaybeSendPriorityUpdateFrame() override;

  // Returns the WebTransport session owned by this stream, if one exists.
  WebTransportHttp3* web_transport() { return web_transport_.get(); }

  // Returns the WebTransport data stream associated with this QUIC stream, or
  // null if this is not a WebTransport data stream.
  WebTransportStream* web_transport_stream() {
    if (web_transport_data_ == nullptr) {
      return nullptr;
    }
    return &web_transport_data_->adapter;
  }

  // Sends a WEBTRANSPORT_STREAM frame and sets up the appropriate metadata.
  void ConvertToWebTransportDataStream(WebTransportSessionId session_id);

  void OnCanWriteNewData() override;

  // If this stream is a WebTransport data stream, closes the connection with an
  // error, and returns false.
  bool AssertNotWebTransportDataStream(absl::string_view operation);

  // Indicates whether a call to WriteBodySlices will be successful and not
  // rejected due to buffer being full.  |write_size| must be non-zero.
  bool CanWriteNewBodyData(QuicByteCount write_size) const;

  // From CapsuleParser::Visitor.
  bool OnCapsule(const quiche::Capsule& capsule) override;
  void OnCapsuleParseFailure(absl::string_view error_message) override;

  // Sends an HTTP/3 datagram. The stream ID is not part of |payload|. Virtual
  // to allow mocking in tests.
  virtual MessageStatus SendHttp3Datagram(absl::string_view payload);

  // Registers |visitor| to receive HTTP/3 datagrams and enables Capsule
  // Protocol by registering a CapsuleParser. |visitor| must be valid until a
  // corresponding call to UnregisterHttp3DatagramVisitor.
  void RegisterHttp3DatagramVisitor(Http3DatagramVisitor* visitor);

  // Unregisters an HTTP/3 datagram visitor. Must only be called after a call to
  // RegisterHttp3DatagramVisitor.
  void UnregisterHttp3DatagramVisitor();

  // Replaces the current HTTP/3 datagram visitor with a different visitor.
  // Mainly meant to be used by the visitors' move operators.
  void ReplaceHttp3DatagramVisitor(Http3DatagramVisitor* visitor);

  // Registers |visitor| to receive CONNECT-IP capsules. |visitor| must be
  // valid until a corresponding call to UnregisterConnectIpVisitor.
  void RegisterConnectIpVisitor(ConnectIpVisitor* visitor);

  // Unregisters a CONNECT-IP visitor. Must only be called after a call to
  // RegisterConnectIpVisitor.
  void UnregisterConnectIpVisitor();

  // Replaces the current CONNECT-IP visitor with a different visitor.
  // Mainly meant to be used by the visitors' move operators.
  void ReplaceConnectIpVisitor(ConnectIpVisitor* visitor);

  // Sets max datagram time in queue.
  void SetMaxDatagramTimeInQueue(QuicTime::Delta max_time_in_queue);

  void OnDatagramReceived(QuicDataReader* reader);

  QuicByteCount GetMaxDatagramSize() const;

  // Writes |capsule| onto the DATA stream.
  void WriteCapsule(const quiche::Capsule& capsule, bool fin = false);

  void WriteGreaseCapsule();

  const std::string& invalid_request_details() const {
    return invalid_request_details_;
  }

  // Registers |visitor| to receive HTTP/3 METADATA. |visitor| must be valid
  // until a corresponding call to UnregisterRegisterMetadataVisitor.
  void RegisterMetadataVisitor(MetadataVisitor* visitor);
  void UnregisterMetadataVisitor();

  // Returns how long header decoding was delayed due to waiting for data to
  // arrive on the QPACK encoder stream.
  // Returns zero if header block could be decoded as soon as it was received.
  // Returns `nullopt` if header block is not decoded yet.
  std::optional<QuicTime::Delta> header_decoding_delay() const {
    return header_decoding_delay_;
  }

 protected:
  // Called when the received headers are too large. By default this will
  // reset the stream.
  virtual void OnHeadersTooLarge();

  virtual void OnInitialHeadersComplete(bool fin, size_t frame_len,
                                        const QuicHeaderList& header_list);
  virtual void OnTrailingHeadersComplete(bool fin, size_t frame_len,
                                         const QuicHeaderList& header_list);
  virtual size_t WriteHeadersImpl(
      spdy::Http2HeaderBlock header_block, bool fin,
      quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
          ack_listener);

  virtual bool CopyAndValidateTrailers(const QuicHeaderList& header_list,
                                       bool expect_final_byte_offset,
                                       size_t* final_byte_offset,
                                       spdy::Http2HeaderBlock* trailers);

  Visitor* visitor() { return visitor_; }

  void set_headers_decompressed(bool val) { headers_decompressed_ = val; }

  virtual bool uses_capsules() const { return capsule_parser_ != nullptr; }

  void set_ack_listener(
      quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
          ack_listener) {
    ack_listener_ = std::move(ack_listener);
  }

  void OnWriteSideInDataRecvdState() override;

  virtual bool ValidateReceivedHeaders(const QuicHeaderList& header_list);
  // TODO(b/202433856) Merge AreHeaderFieldValueValid into
  // ValidateReceivedHeaders once all flags guarding the behavior of
  // ValidateReceivedHeaders has been rolled out.
  virtual bool AreHeaderFieldValuesValid(
      const QuicHeaderList& header_list) const;

  // Reset stream upon invalid request headers.
  virtual void OnInvalidHeaders();

  void set_invalid_request_details(std::string invalid_request_details);

  // Called by HttpDecoderVisitor.
  virtual bool OnDataFrameStart(QuicByteCount header_length,
                                QuicByteCount payload_length);

  void CloseReadSide() override;

 private:
  friend class test::QuicSpdyStreamPeer;
  friend class test::QuicStreamPeer;
  friend class QuicStreamUtils;
  class HttpDecoderVisitor;

  struct QUICHE_EXPORT WebTransportDataStream {
    WebTransportDataStream(QuicSpdyStream* stream,
                           WebTransportSessionId session_id);

    WebTransportSessionId session_id;
    WebTransportStreamAdapter adapter;
  };

  // Called by HttpDecoderVisitor.
  bool OnDataFramePayload(absl::string_view payload);
  bool OnDataFrameEnd();
  bool OnHeadersFrameStart(QuicByteCount header_length,
                           QuicByteCount payload_length);
  bool OnHeadersFramePayload(absl::string_view payload);
  bool OnHeadersFrameEnd();
  void OnWebTransportStreamFrameType(QuicByteCount header_length,
                                     WebTransportSessionId session_id);
  bool OnMetadataFrameStart(QuicByteCount header_length,
                            QuicByteCount payload_length);
  bool OnMetadataFramePayload(absl::string_view payload);
  bool OnMetadataFrameEnd();
  bool OnUnknownFrameStart(uint64_t frame_type, QuicByteCount header_length,
                           QuicByteCount payload_length);
  bool OnUnknownFramePayload(absl::string_view payload);
  bool OnUnknownFrameEnd();

  // Given the interval marked by [|offset|, |offset| + |data_length|), return
  // the number of frame header bytes contained in it.
  QuicByteCount GetNumFrameHeadersInInterval(QuicStreamOffset offset,
                                             QuicByteCount data_length) const;

  void MaybeProcessSentWebTransportHeaders(spdy::Http2HeaderBlock& headers);
  void MaybeProcessReceivedWebTransportHeaders();

  // Writes HTTP/3 DATA frame header. If |force_write| is true, use
  // WriteOrBufferData if send buffer cannot accomodate the header + data.
  ABSL_MUST_USE_RESULT bool WriteDataFrameHeader(QuicByteCount data_length,
                                                 bool force_write);

  // Simply calls OnBodyAvailable() unless capsules are in use, in which case
  // pass the capsule fragments to the capsule manager.
  void HandleBodyAvailable();

  // Called when a datagram frame or capsule is received.
  void HandleReceivedDatagram(absl::string_view payload);

  // Whether the next received header is trailer or not.
  virtual bool NextHeaderIsTrailer() const { return headers_decompressed_; }

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
  // Length of most recently received HEADERS frame payload.
  QuicByteCount headers_payload_length_;

  // True if the trailers have been completely decompressed.
  bool trailers_decompressed_;
  // True if the trailers have been consumed.
  bool trailers_consumed_;

  // The parsed trailers received from the peer.
  spdy::Http2HeaderBlock received_trailers_;

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

  std::unique_ptr<quiche::CapsuleParser> capsule_parser_;

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
  quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface> ack_listener_;

  // Offset of unacked frame headers.
  QuicIntervalSet<QuicStreamOffset> unacked_frame_headers_offsets_;

  // Priority parameters sent in the last PRIORITY_UPDATE frame, or default
  // values defined by RFC9218 if no PRIORITY_UPDATE frame has been sent.
  QuicStreamPriority last_sent_priority_;

  // If this stream is a WebTransport extended CONNECT stream, contains the
  // WebTransport session associated with this stream.
  std::unique_ptr<WebTransportHttp3> web_transport_;

  // If this stream is a WebTransport data stream, |web_transport_data_|
  // contains all of the associated metadata.
  std::unique_ptr<WebTransportDataStream> web_transport_data_;

  // HTTP/3 Datagram support.
  Http3DatagramVisitor* datagram_visitor_ = nullptr;
  // CONNECT-IP support.
  ConnectIpVisitor* connect_ip_visitor_ = nullptr;

  // Present if HTTP/3 METADATA frames should be parsed.
  MetadataVisitor* metadata_visitor_ = nullptr;

  // Present if an HTTP/3 METADATA is currently being parsed.
  std::unique_ptr<MetadataDecoder> metadata_decoder_;

  // Empty if the headers are valid.
  std::string invalid_request_details_;

  // Time when entire header block was received.
  // Only set if decoding was blocked.
  QuicTime header_block_received_time_ = QuicTime::Zero();

  // Header decoding delay due to waiting for data on the QPACK encoder stream.
  // Zero if header block could be decoded as soon as it was received.
  // `nullopt` if header block is not decoded yet.
  std::optional<QuicTime::Delta> header_decoding_delay_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_HTTP_QUIC_SPDY_STREAM_H_
