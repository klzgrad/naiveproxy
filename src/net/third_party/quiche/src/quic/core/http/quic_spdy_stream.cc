// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_stream.h"

#include <limits>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/http/http_constants.h"
#include "net/third_party/quiche/src/quic/core/http/http_decoder.h"
#include "net/third_party/quiche/src/quic/core/http/quic_spdy_session.h"
#include "net/third_party/quiche/src/quic/core/http/spdy_utils.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/core/quic_write_blocked_list.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice_storage.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_arraysize.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"

using spdy::SpdyHeaderBlock;
using spdy::SpdyPriority;

namespace quic {

// Visitor of HttpDecoder that passes data frame to QuicSpdyStream and closes
// the connection on unexpected frames.
class QuicSpdyStream::HttpDecoderVisitor : public HttpDecoder::Visitor {
 public:
  explicit HttpDecoderVisitor(QuicSpdyStream* stream) : stream_(stream) {}
  HttpDecoderVisitor(const HttpDecoderVisitor&) = delete;
  HttpDecoderVisitor& operator=(const HttpDecoderVisitor&) = delete;

  void OnError(HttpDecoder* decoder) override {
    stream_->OnUnrecoverableError(decoder->error(), decoder->error_detail());
  }

  bool OnCancelPushFrame(const CancelPushFrame& /*frame*/) override {
    CloseConnectionOnWrongFrame("Cancel Push");
    return false;
  }

  bool OnMaxPushIdFrame(const MaxPushIdFrame& /*frame*/) override {
    CloseConnectionOnWrongFrame("Max Push Id");
    return false;
  }

  bool OnGoAwayFrame(const GoAwayFrame& /*frame*/) override {
    CloseConnectionOnWrongFrame("Goaway");
    return false;
  }

  bool OnSettingsFrameStart(QuicByteCount /*header_length*/) override {
    CloseConnectionOnWrongFrame("Settings");
    return false;
  }

  bool OnSettingsFrame(const SettingsFrame& /*frame*/) override {
    CloseConnectionOnWrongFrame("Settings");
    return false;
  }

  bool OnDataFrameStart(QuicByteCount header_length,
                        QuicByteCount payload_length) override {
    return stream_->OnDataFrameStart(header_length, payload_length);
  }

  bool OnDataFramePayload(quiche::QuicheStringPiece payload) override {
    DCHECK(!payload.empty());
    return stream_->OnDataFramePayload(payload);
  }

  bool OnDataFrameEnd() override { return stream_->OnDataFrameEnd(); }

  bool OnHeadersFrameStart(QuicByteCount header_length,
                           QuicByteCount payload_length) override {
    if (!VersionUsesHttp3(stream_->transport_version())) {
      CloseConnectionOnWrongFrame("Headers");
      return false;
    }
    return stream_->OnHeadersFrameStart(header_length, payload_length);
  }

  bool OnHeadersFramePayload(quiche::QuicheStringPiece payload) override {
    DCHECK(!payload.empty());
    if (!VersionUsesHttp3(stream_->transport_version())) {
      CloseConnectionOnWrongFrame("Headers");
      return false;
    }
    return stream_->OnHeadersFramePayload(payload);
  }

  bool OnHeadersFrameEnd() override {
    if (!VersionUsesHttp3(stream_->transport_version())) {
      CloseConnectionOnWrongFrame("Headers");
      return false;
    }
    return stream_->OnHeadersFrameEnd();
  }

  bool OnPushPromiseFrameStart(QuicByteCount header_length) override {
    if (!VersionUsesHttp3(stream_->transport_version())) {
      CloseConnectionOnWrongFrame("Push Promise");
      return false;
    }
    return stream_->OnPushPromiseFrameStart(header_length);
  }

  bool OnPushPromiseFramePushId(PushId push_id,
                                QuicByteCount push_id_length,
                                QuicByteCount header_block_length) override {
    if (!VersionUsesHttp3(stream_->transport_version())) {
      CloseConnectionOnWrongFrame("Push Promise");
      return false;
    }
    return stream_->OnPushPromiseFramePushId(push_id, push_id_length,
                                             header_block_length);
  }

  bool OnPushPromiseFramePayload(quiche::QuicheStringPiece payload) override {
    DCHECK(!payload.empty());
    if (!VersionUsesHttp3(stream_->transport_version())) {
      CloseConnectionOnWrongFrame("Push Promise");
      return false;
    }
    return stream_->OnPushPromiseFramePayload(payload);
  }

  bool OnPushPromiseFrameEnd() override {
    if (!VersionUsesHttp3(stream_->transport_version())) {
      CloseConnectionOnWrongFrame("Push Promise");
      return false;
    }
    return stream_->OnPushPromiseFrameEnd();
  }

  bool OnPriorityUpdateFrameStart(QuicByteCount /*header_length*/) override {
    CloseConnectionOnWrongFrame("Priority update");
    return false;
  }

  bool OnPriorityUpdateFrame(const PriorityUpdateFrame& /*frame*/) override {
    CloseConnectionOnWrongFrame("Priority update");
    return false;
  }

  bool OnUnknownFrameStart(uint64_t frame_type,
                           QuicByteCount header_length,
                           QuicByteCount payload_length) override {
    return stream_->OnUnknownFrameStart(frame_type, header_length,
                                        payload_length);
  }

  bool OnUnknownFramePayload(quiche::QuicheStringPiece payload) override {
    return stream_->OnUnknownFramePayload(payload);
  }

  bool OnUnknownFrameEnd() override { return stream_->OnUnknownFrameEnd(); }

 private:
  void CloseConnectionOnWrongFrame(quiche::QuicheStringPiece frame_type) {
    stream_->OnUnrecoverableError(
        QUIC_HTTP_FRAME_UNEXPECTED_ON_SPDY_STREAM,
        quiche::QuicheStrCat(frame_type, " frame received on data stream"));
  }

  QuicSpdyStream* stream_;
};

#define ENDPOINT                                                   \
  (session()->perspective() == Perspective::IS_SERVER ? "Server: " \
                                                      : "Client:"  \
                                                        " ")

QuicSpdyStream::QuicSpdyStream(QuicStreamId id,
                               QuicSpdySession* spdy_session,
                               StreamType type)
    : QuicStream(id, spdy_session, /*is_static=*/false, type),
      spdy_session_(spdy_session),
      on_body_available_called_because_sequencer_is_closed_(false),
      visitor_(nullptr),
      blocked_on_decoding_headers_(false),
      headers_decompressed_(false),
      header_list_size_limit_exceeded_(false),
      headers_payload_length_(0),
      trailers_payload_length_(0),
      trailers_decompressed_(false),
      trailers_consumed_(false),
      http_decoder_visitor_(std::make_unique<HttpDecoderVisitor>(this)),
      decoder_(http_decoder_visitor_.get()),
      sequencer_offset_(0),
      is_decoder_processing_input_(false),
      ack_listener_(nullptr),
      last_sent_urgency_(kDefaultUrgency) {
  DCHECK_EQ(session()->connection(), spdy_session->connection());
  DCHECK_EQ(transport_version(), spdy_session->transport_version());
  DCHECK(!QuicUtils::IsCryptoStreamId(transport_version(), id));
  DCHECK_EQ(0u, sequencer()->NumBytesConsumed());
  // If headers are sent on the headers stream, then do not receive any
  // callbacks from the sequencer until headers are complete.
  if (!VersionUsesHttp3(transport_version())) {
    sequencer()->SetBlockedUntilFlush();
  }

  if (VersionUsesHttp3(transport_version())) {
    sequencer()->set_level_triggered(true);
  }

  spdy_session_->OnStreamCreated(this);
}

QuicSpdyStream::QuicSpdyStream(PendingStream* pending,
                               QuicSpdySession* spdy_session,
                               StreamType type)
    : QuicStream(pending, type, /*is_static=*/false),
      spdy_session_(spdy_session),
      on_body_available_called_because_sequencer_is_closed_(false),
      visitor_(nullptr),
      blocked_on_decoding_headers_(false),
      headers_decompressed_(false),
      header_list_size_limit_exceeded_(false),
      headers_payload_length_(0),
      trailers_payload_length_(0),
      trailers_decompressed_(false),
      trailers_consumed_(false),
      http_decoder_visitor_(std::make_unique<HttpDecoderVisitor>(this)),
      decoder_(http_decoder_visitor_.get()),
      sequencer_offset_(sequencer()->NumBytesConsumed()),
      is_decoder_processing_input_(false),
      ack_listener_(nullptr),
      last_sent_urgency_(kDefaultUrgency) {
  DCHECK_EQ(session()->connection(), spdy_session->connection());
  DCHECK_EQ(transport_version(), spdy_session->transport_version());
  DCHECK(!QuicUtils::IsCryptoStreamId(transport_version(), id()));
  // If headers are sent on the headers stream, then do not receive any
  // callbacks from the sequencer until headers are complete.
  if (!VersionUsesHttp3(transport_version())) {
    sequencer()->SetBlockedUntilFlush();
  }

  if (VersionUsesHttp3(transport_version())) {
    sequencer()->set_level_triggered(true);
  }

  spdy_session_->OnStreamCreated(this);
}

QuicSpdyStream::~QuicSpdyStream() {}

size_t QuicSpdyStream::WriteHeaders(
    SpdyHeaderBlock header_block,
    bool fin,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  QuicConnection::ScopedPacketFlusher flusher(spdy_session_->connection());
  // Send stream type for server push stream
  if (VersionUsesHttp3(transport_version()) && type() == WRITE_UNIDIRECTIONAL &&
      send_buffer().stream_offset() == 0) {
    char data[sizeof(kServerPushStream)];
    QuicDataWriter writer(QUICHE_ARRAYSIZE(data), data);
    writer.WriteVarInt62(kServerPushStream);

    // Similar to frame headers, stream type byte shouldn't be exposed to upper
    // layer applications.
    unacked_frame_headers_offsets_.Add(0, writer.length());

    QUIC_LOG(INFO) << ENDPOINT << "Stream " << id()
                   << " is writing type as server push";
    WriteOrBufferData(quiche::QuicheStringPiece(writer.data(), writer.length()),
                      false, nullptr);
  }

  size_t bytes_written =
      WriteHeadersImpl(std::move(header_block), fin, std::move(ack_listener));
  if (!VersionUsesHttp3(transport_version()) && fin) {
    // If HEADERS are sent on the headers stream, then |fin_sent_| needs to be
    // set and write side needs to be closed without actually sending a FIN on
    // this stream.
    // TODO(rch): Add test to ensure fin_sent_ is set whenever a fin is sent.
    set_fin_sent(true);
    CloseWriteSide();
  }
  return bytes_written;
}

void QuicSpdyStream::WriteOrBufferBody(quiche::QuicheStringPiece data,
                                       bool fin) {
  if (!VersionUsesHttp3(transport_version()) || data.length() == 0) {
    WriteOrBufferData(data, fin, nullptr);
    return;
  }
  QuicConnection::ScopedPacketFlusher flusher(spdy_session_->connection());

  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnDataFrameSent(id(), data.length());
  }

  // Write frame header.
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeDataFrameHeader(data.length(), &buffer);
  unacked_frame_headers_offsets_.Add(
      send_buffer().stream_offset(),
      send_buffer().stream_offset() + header_length);
  QUIC_DLOG(INFO) << ENDPOINT << "Stream " << id()
                  << " is writing DATA frame header of length "
                  << header_length;
  WriteOrBufferData(quiche::QuicheStringPiece(buffer.get(), header_length),
                    false, nullptr);

  // Write body.
  QUIC_DLOG(INFO) << ENDPOINT << "Stream " << id()
                  << " is writing DATA frame payload of length "
                  << data.length() << " with fin " << fin;
  WriteOrBufferData(data, fin, nullptr);
}

size_t QuicSpdyStream::WriteTrailers(
    SpdyHeaderBlock trailer_block,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  if (fin_sent()) {
    QUIC_BUG << "Trailers cannot be sent after a FIN, on stream " << id();
    return 0;
  }

  if (!VersionUsesHttp3(transport_version())) {
    // The header block must contain the final offset for this stream, as the
    // trailers may be processed out of order at the peer.
    const QuicStreamOffset final_offset =
        stream_bytes_written() + BufferedDataBytes();
    QUIC_DLOG(INFO) << ENDPOINT << "Inserting trailer: ("
                    << kFinalOffsetHeaderKey << ", " << final_offset << ")";
    trailer_block.insert(
        std::make_pair(kFinalOffsetHeaderKey,
                       quiche::QuicheTextUtils::Uint64ToString(final_offset)));
  }

  // Write the trailing headers with a FIN, and close stream for writing:
  // trailers are the last thing to be sent on a stream.
  const bool kFin = true;
  size_t bytes_written =
      WriteHeadersImpl(std::move(trailer_block), kFin, std::move(ack_listener));

  // If trailers are sent on the headers stream, then |fin_sent_| needs to be
  // set without actually sending a FIN on this stream.
  if (!VersionUsesHttp3(transport_version())) {
    set_fin_sent(kFin);

    // Also, write side of this stream needs to be closed.  However, only do
    // this if there is no more buffered data, otherwise it will never be sent.
    if (BufferedDataBytes() == 0) {
      CloseWriteSide();
    }
  }

  return bytes_written;
}

void QuicSpdyStream::WritePushPromise(const PushPromiseFrame& frame) {
  DCHECK(VersionUsesHttp3(transport_version()));
  std::unique_ptr<char[]> push_promise_frame_with_id;
  const size_t push_promise_frame_length =
      HttpEncoder::SerializePushPromiseFrameWithOnlyPushId(
          frame, &push_promise_frame_with_id);

  unacked_frame_headers_offsets_.Add(send_buffer().stream_offset(),
                                     send_buffer().stream_offset() +
                                         push_promise_frame_length +
                                         frame.headers.length());

  // Write Push Promise frame header and push id.
  QUIC_DLOG(INFO) << ENDPOINT << "Stream " << id()
                  << " is writing Push Promise frame header of length "
                  << push_promise_frame_length << " , with promised id "
                  << frame.push_id;
  WriteOrBufferData(quiche::QuicheStringPiece(push_promise_frame_with_id.get(),
                                              push_promise_frame_length),
                    /* fin = */ false, /* ack_listener = */ nullptr);

  // Write response headers.
  QUIC_DLOG(INFO) << ENDPOINT << "Stream " << id()
                  << " is writing Push Promise request header of length "
                  << frame.headers.length();
  WriteOrBufferData(frame.headers, /* fin = */ false,
                    /* ack_listener = */ nullptr);
}

QuicConsumedData QuicSpdyStream::WritevBody(const struct iovec* iov,
                                            int count,
                                            bool fin) {
  QuicMemSliceStorage storage(
      iov, count,
      session()->connection()->helper()->GetStreamSendBufferAllocator(),
      GetQuicFlag(FLAGS_quic_send_buffer_max_data_slice_size));
  return WriteBodySlices(storage.ToSpan(), fin);
}

QuicConsumedData QuicSpdyStream::WriteBodySlices(QuicMemSliceSpan slices,
                                                 bool fin) {
  if (!VersionUsesHttp3(transport_version()) || slices.empty()) {
    return WriteMemSlices(slices, fin);
  }

  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      HttpEncoder::SerializeDataFrameHeader(slices.total_length(), &buffer);
  if (!CanWriteNewDataAfterData(header_length)) {
    return {0, false};
  }

  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnDataFrameSent(id(),
                                                    slices.total_length());
  }

  QuicConnection::ScopedPacketFlusher flusher(spdy_session_->connection());

  // Write frame header.
  struct iovec header_iov = {static_cast<void*>(buffer.get()), header_length};
  QuicMemSliceStorage storage(
      &header_iov, 1,
      spdy_session_->connection()->helper()->GetStreamSendBufferAllocator(),
      GetQuicFlag(FLAGS_quic_send_buffer_max_data_slice_size));
  unacked_frame_headers_offsets_.Add(
      send_buffer().stream_offset(),
      send_buffer().stream_offset() + header_length);
  QUIC_DLOG(INFO) << ENDPOINT << "Stream " << id()
                  << " is writing DATA frame header of length "
                  << header_length;
  WriteMemSlices(storage.ToSpan(), false);

  // Write body.
  QUIC_DLOG(INFO) << ENDPOINT << "Stream " << id()
                  << " is writing DATA frame payload of length "
                  << slices.total_length();
  return WriteMemSlices(slices, fin);
}

size_t QuicSpdyStream::Readv(const struct iovec* iov, size_t iov_len) {
  DCHECK(FinishedReadingHeaders());
  if (!VersionUsesHttp3(transport_version())) {
    return sequencer()->Readv(iov, iov_len);
  }
  size_t bytes_read = 0;
  sequencer()->MarkConsumed(body_manager_.ReadBody(iov, iov_len, &bytes_read));

  return bytes_read;
}

int QuicSpdyStream::GetReadableRegions(iovec* iov, size_t iov_len) const {
  DCHECK(FinishedReadingHeaders());
  if (!VersionUsesHttp3(transport_version())) {
    return sequencer()->GetReadableRegions(iov, iov_len);
  }
  return body_manager_.PeekBody(iov, iov_len);
}

void QuicSpdyStream::MarkConsumed(size_t num_bytes) {
  DCHECK(FinishedReadingHeaders());
  if (!VersionUsesHttp3(transport_version())) {
    sequencer()->MarkConsumed(num_bytes);
    return;
  }

  sequencer()->MarkConsumed(body_manager_.OnBodyConsumed(num_bytes));
}

bool QuicSpdyStream::IsDoneReading() const {
  bool done_reading_headers = FinishedReadingHeaders();
  bool done_reading_body = sequencer()->IsClosed();
  bool done_reading_trailers = FinishedReadingTrailers();
  return done_reading_headers && done_reading_body && done_reading_trailers;
}

bool QuicSpdyStream::HasBytesToRead() const {
  if (!VersionUsesHttp3(transport_version())) {
    return sequencer()->HasBytesToRead();
  }
  return body_manager_.HasBytesToRead();
}

void QuicSpdyStream::MarkTrailersConsumed() {
  trailers_consumed_ = true;
}

uint64_t QuicSpdyStream::total_body_bytes_read() const {
  if (VersionUsesHttp3(transport_version())) {
    return body_manager_.total_body_bytes_received();
  }
  return sequencer()->NumBytesConsumed();
}

void QuicSpdyStream::ConsumeHeaderList() {
  header_list_.Clear();

  if (!FinishedReadingHeaders()) {
    return;
  }

  if (!VersionUsesHttp3(transport_version())) {
    sequencer()->SetUnblocked();
    return;
  }

  if (body_manager_.HasBytesToRead()) {
    OnBodyAvailable();
    return;
  }

  if (sequencer()->IsClosed() &&
      !on_body_available_called_because_sequencer_is_closed_) {
    on_body_available_called_because_sequencer_is_closed_ = true;
    OnBodyAvailable();
  }
}

void QuicSpdyStream::OnStreamHeadersPriority(
    const spdy::SpdyStreamPrecedence& precedence) {
  DCHECK_EQ(Perspective::IS_SERVER, session()->connection()->perspective());
  SetPriority(precedence);
}

void QuicSpdyStream::OnStreamHeaderList(bool fin,
                                        size_t frame_len,
                                        const QuicHeaderList& header_list) {
  // TODO(b/134706391): remove |fin| argument.
  // When using Google QUIC, an empty header list indicates that the size limit
  // has been exceeded.
  // When using IETF QUIC, there is an explicit signal from
  // QpackDecodedHeadersAccumulator.
  if ((VersionUsesHttp3(transport_version()) &&
       header_list_size_limit_exceeded_) ||
      (!VersionUsesHttp3(transport_version()) && header_list.empty())) {
    OnHeadersTooLarge();
    if (IsDoneReading()) {
      return;
    }
  }
  if (!headers_decompressed_) {
    OnInitialHeadersComplete(fin, frame_len, header_list);
  } else {
    OnTrailingHeadersComplete(fin, frame_len, header_list);
  }
}

void QuicSpdyStream::OnHeadersDecoded(QuicHeaderList headers,
                                      bool header_list_size_limit_exceeded) {
  header_list_size_limit_exceeded_ = header_list_size_limit_exceeded;
  qpack_decoded_headers_accumulator_.reset();

  QuicSpdySession::LogHeaderCompressionRatioHistogram(
      /* using_qpack = */ true,
      /* is_sent = */ false, headers.compressed_header_bytes(),
      headers.uncompressed_header_bytes());

  const QuicStreamId promised_stream_id = spdy_session()->promised_stream_id();
  Http3DebugVisitor* const debug_visitor = spdy_session()->debug_visitor();
  if (promised_stream_id ==
      QuicUtils::GetInvalidStreamId(transport_version())) {
    if (debug_visitor) {
      debug_visitor->OnHeadersDecoded(id(), headers);
    }

    const QuicByteCount frame_length = headers_decompressed_
                                           ? trailers_payload_length_
                                           : headers_payload_length_;
    OnStreamHeaderList(/* fin = */ false, frame_length, headers);
  } else {
    if (debug_visitor) {
      debug_visitor->OnPushPromiseDecoded(id(), promised_stream_id, headers);
    }

    spdy_session_->OnHeaderList(headers);
  }

  if (blocked_on_decoding_headers_) {
    blocked_on_decoding_headers_ = false;
    // Continue decoding HTTP/3 frames.
    OnDataAvailable();
  }
}

void QuicSpdyStream::OnHeaderDecodingError(
    quiche::QuicheStringPiece error_message) {
  qpack_decoded_headers_accumulator_.reset();

  std::string connection_close_error_message = quiche::QuicheStrCat(
      "Error decoding ", headers_decompressed_ ? "trailers" : "headers",
      " on stream ", id(), ": ", error_message);
  OnUnrecoverableError(QUIC_QPACK_DECOMPRESSION_FAILED,
                       connection_close_error_message);
}

void QuicSpdyStream::MaybeSendPriorityUpdateFrame() {
  if (!VersionUsesHttp3(transport_version()) ||
      session()->perspective() != Perspective::IS_CLIENT) {
    return;
  }

  // Value between 0 and 7, inclusive.  Lower value means higher priority.
  int urgency = precedence().spdy3_priority();
  if (last_sent_urgency_ == urgency) {
    return;
  }
  last_sent_urgency_ = urgency;

  PriorityUpdateFrame priority_update;
  priority_update.prioritized_element_type = REQUEST_STREAM;
  priority_update.prioritized_element_id = id();
  priority_update.priority_field_value = quiche::QuicheStrCat("u=", urgency);
  spdy_session_->WriteHttp3PriorityUpdate(priority_update);
}

void QuicSpdyStream::OnHeadersTooLarge() {
  if (VersionUsesHttp3(transport_version())) {
    // TODO(b/124216424): Reset stream with H3_REQUEST_CANCELLED (if client)
    // or with H3_REQUEST_REJECTED (if server).
    std::string error_message =
        quiche::QuicheStrCat("Too large headers received on stream ", id());
    OnUnrecoverableError(QUIC_HEADERS_STREAM_DATA_DECOMPRESS_FAILURE,
                         error_message);
  } else {
    Reset(QUIC_HEADERS_TOO_LARGE);
  }
}

void QuicSpdyStream::OnInitialHeadersComplete(
    bool fin,
    size_t /*frame_len*/,
    const QuicHeaderList& header_list) {
  // TODO(b/134706391): remove |fin| argument.
  headers_decompressed_ = true;
  header_list_ = header_list;

  if (VersionUsesHttp3(transport_version())) {
    if (fin) {
      OnStreamFrame(
          QuicStreamFrame(id(), /* fin = */ true,
                          flow_controller()->highest_received_byte_offset(),
                          quiche::QuicheStringPiece()));
    }
    return;
  }

  if (fin && !rst_sent()) {
    OnStreamFrame(QuicStreamFrame(id(), fin, /* offset = */ 0,
                                  quiche::QuicheStringPiece()));
  }
  if (FinishedReadingHeaders()) {
    sequencer()->SetUnblocked();
  }
}

void QuicSpdyStream::OnPromiseHeaderList(
    QuicStreamId /* promised_id */,
    size_t /* frame_len */,
    const QuicHeaderList& /*header_list */) {
  // To be overridden in QuicSpdyClientStream.  Not supported on
  // server side.
  stream_delegate()->OnStreamError(QUIC_INVALID_HEADERS_STREAM_DATA,
                                   "Promise headers received by server");
}

void QuicSpdyStream::OnTrailingHeadersComplete(
    bool fin,
    size_t /*frame_len*/,
    const QuicHeaderList& header_list) {
  // TODO(b/134706391): remove |fin| argument.
  DCHECK(!trailers_decompressed_);
  if (!VersionUsesHttp3(transport_version()) && fin_received()) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Received Trailers after FIN, on stream: " << id();
    stream_delegate()->OnStreamError(QUIC_INVALID_HEADERS_STREAM_DATA,
                                     "Trailers after fin");
    return;
  }

  if (!VersionUsesHttp3(transport_version()) && !fin) {
    QUIC_DLOG(INFO) << ENDPOINT
                    << "Trailers must have FIN set, on stream: " << id();
    stream_delegate()->OnStreamError(QUIC_INVALID_HEADERS_STREAM_DATA,
                                     "Fin missing from trailers");
    return;
  }

  size_t final_byte_offset = 0;
  const bool expect_final_byte_offset = !VersionUsesHttp3(transport_version());
  if (!SpdyUtils::CopyAndValidateTrailers(header_list, expect_final_byte_offset,
                                          &final_byte_offset,
                                          &received_trailers_)) {
    QUIC_DLOG(ERROR) << ENDPOINT << "Trailers for stream " << id()
                     << " are malformed.";
    stream_delegate()->OnStreamError(QUIC_INVALID_HEADERS_STREAM_DATA,
                                     "Trailers are malformed");
    return;
  }
  trailers_decompressed_ = true;
  if (fin) {
    const QuicStreamOffset offset =
        VersionUsesHttp3(transport_version())
            ? flow_controller()->highest_received_byte_offset()
            : final_byte_offset;
    OnStreamFrame(
        QuicStreamFrame(id(), fin, offset, quiche::QuicheStringPiece()));
  }
}

void QuicSpdyStream::OnPriorityFrame(
    const spdy::SpdyStreamPrecedence& precedence) {
  DCHECK_EQ(Perspective::IS_SERVER, session()->connection()->perspective());
  SetPriority(precedence);
}

void QuicSpdyStream::OnStreamReset(const QuicRstStreamFrame& frame) {
  if (frame.error_code != QUIC_STREAM_NO_ERROR) {
    if (VersionUsesHttp3(transport_version()) && !fin_received() &&
        spdy_session_->qpack_decoder()) {
      spdy_session_->qpack_decoder()->OnStreamReset(id());
    }

    QuicStream::OnStreamReset(frame);
    return;
  }

  QUIC_DVLOG(1) << ENDPOINT
                << "Received QUIC_STREAM_NO_ERROR, not discarding response";
  set_rst_received(true);
  MaybeIncreaseHighestReceivedOffset(frame.byte_offset);
  set_stream_error(frame.error_code);
  CloseWriteSide();
}

void QuicSpdyStream::Reset(QuicRstStreamErrorCode error) {
  if (VersionUsesHttp3(transport_version()) && !fin_received() &&
      spdy_session_->qpack_decoder()) {
    spdy_session_->qpack_decoder()->OnStreamReset(id());
  }

  QuicStream::Reset(error);
}

void QuicSpdyStream::OnDataAvailable() {
  if (!VersionUsesHttp3(transport_version())) {
    // Sequencer must be blocked until headers are consumed.
    DCHECK(FinishedReadingHeaders());
  }

  if (!VersionUsesHttp3(transport_version())) {
    OnBodyAvailable();
    return;
  }

  if (is_decoder_processing_input_) {
    // Let the outermost nested OnDataAvailable() call do the work.
    return;
  }

  if (blocked_on_decoding_headers_) {
    return;
  }

  iovec iov;
  while (session()->connection()->connected() && !reading_stopped() &&
         decoder_.error() == QUIC_NO_ERROR) {
    DCHECK_GE(sequencer_offset_, sequencer()->NumBytesConsumed());
    if (!sequencer()->PeekRegion(sequencer_offset_, &iov)) {
      break;
    }

    DCHECK(!sequencer()->IsClosed());
    is_decoder_processing_input_ = true;
    QuicByteCount processed_bytes = decoder_.ProcessInput(
        reinterpret_cast<const char*>(iov.iov_base), iov.iov_len);
    is_decoder_processing_input_ = false;
    sequencer_offset_ += processed_bytes;
    if (blocked_on_decoding_headers_) {
      return;
    }
  }

  // Do not call OnBodyAvailable() until headers are consumed.
  if (!FinishedReadingHeaders()) {
    return;
  }

  if (body_manager_.HasBytesToRead()) {
    OnBodyAvailable();
    return;
  }

  if (sequencer()->IsClosed() &&
      !on_body_available_called_because_sequencer_is_closed_) {
    on_body_available_called_because_sequencer_is_closed_ = true;
    OnBodyAvailable();
  }
}

void QuicSpdyStream::OnClose() {
  QuicStream::OnClose();

  if (visitor_) {
    Visitor* visitor = visitor_;
    // Calling Visitor::OnClose() may result the destruction of the visitor,
    // so we need to ensure we don't call it again.
    visitor_ = nullptr;
    visitor->OnClose(this);
  }
}

void QuicSpdyStream::OnCanWrite() {
  QuicStream::OnCanWrite();

  // Trailers (and hence a FIN) may have been sent ahead of queued body bytes.
  if (!HasBufferedData() && fin_sent()) {
    CloseWriteSide();
  }
}

bool QuicSpdyStream::FinishedReadingHeaders() const {
  return headers_decompressed_ && header_list_.empty();
}

// static
bool QuicSpdyStream::ParseHeaderStatusCode(const SpdyHeaderBlock& header,
                                           int* status_code) {
  SpdyHeaderBlock::const_iterator it = header.find(spdy::kHttp2StatusHeader);
  if (it == header.end()) {
    return false;
  }
  const quiche::QuicheStringPiece status(it->second);
  if (status.size() != 3) {
    return false;
  }
  // First character must be an integer in range [1,5].
  if (status[0] < '1' || status[0] > '5') {
    return false;
  }
  // The remaining two characters must be integers.
  if (!isdigit(status[1]) || !isdigit(status[2])) {
    return false;
  }
  return quiche::QuicheTextUtils::StringToInt(status, status_code);
}

bool QuicSpdyStream::FinishedReadingTrailers() const {
  // If no further trailing headers are expected, and the decompressed trailers
  // (if any) have been consumed, then reading of trailers is finished.
  if (!fin_received()) {
    return false;
  } else if (!trailers_decompressed_) {
    return true;
  } else {
    return trailers_consumed_;
  }
}

void QuicSpdyStream::ClearSession() {
  spdy_session_ = nullptr;
}

bool QuicSpdyStream::OnDataFrameStart(QuicByteCount header_length,
                                      QuicByteCount payload_length) {
  DCHECK(VersionUsesHttp3(transport_version()));

  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnDataFrameReceived(id(), payload_length);
  }

  if (!headers_decompressed_ || trailers_decompressed_) {
    stream_delegate()->OnStreamError(
        QUIC_HTTP_INVALID_FRAME_SEQUENCE_ON_SPDY_STREAM,
        "Unexpected DATA frame received.");
    return false;
  }

  sequencer()->MarkConsumed(body_manager_.OnNonBody(header_length));

  return true;
}

bool QuicSpdyStream::OnDataFramePayload(quiche::QuicheStringPiece payload) {
  DCHECK(VersionUsesHttp3(transport_version()));

  body_manager_.OnBody(payload);

  return true;
}

bool QuicSpdyStream::OnDataFrameEnd() {
  DCHECK(VersionUsesHttp3(transport_version()));

  QUIC_DVLOG(1) << ENDPOINT
                << "Reaches the end of a data frame. Total bytes received are "
                << body_manager_.total_body_bytes_received();
  return true;
}

bool QuicSpdyStream::OnStreamFrameAcked(QuicStreamOffset offset,
                                        QuicByteCount data_length,
                                        bool fin_acked,
                                        QuicTime::Delta ack_delay_time,
                                        QuicTime receive_timestamp,
                                        QuicByteCount* newly_acked_length) {
  const bool new_data_acked = QuicStream::OnStreamFrameAcked(
      offset, data_length, fin_acked, ack_delay_time, receive_timestamp,
      newly_acked_length);

  const QuicByteCount newly_acked_header_length =
      GetNumFrameHeadersInInterval(offset, data_length);
  DCHECK_LE(newly_acked_header_length, *newly_acked_length);
  unacked_frame_headers_offsets_.Difference(offset, offset + data_length);
  if (ack_listener_ != nullptr && new_data_acked) {
    ack_listener_->OnPacketAcked(
        *newly_acked_length - newly_acked_header_length, ack_delay_time);
  }
  return new_data_acked;
}

void QuicSpdyStream::OnStreamFrameRetransmitted(QuicStreamOffset offset,
                                                QuicByteCount data_length,
                                                bool fin_retransmitted) {
  QuicStream::OnStreamFrameRetransmitted(offset, data_length,
                                         fin_retransmitted);

  const QuicByteCount retransmitted_header_length =
      GetNumFrameHeadersInInterval(offset, data_length);
  DCHECK_LE(retransmitted_header_length, data_length);

  if (ack_listener_ != nullptr) {
    ack_listener_->OnPacketRetransmitted(data_length -
                                         retransmitted_header_length);
  }
}

QuicByteCount QuicSpdyStream::GetNumFrameHeadersInInterval(
    QuicStreamOffset offset,
    QuicByteCount data_length) const {
  QuicByteCount header_acked_length = 0;
  QuicIntervalSet<QuicStreamOffset> newly_acked(offset, offset + data_length);
  newly_acked.Intersection(unacked_frame_headers_offsets_);
  for (const auto& interval : newly_acked) {
    header_acked_length += interval.Length();
  }
  return header_acked_length;
}

bool QuicSpdyStream::OnHeadersFrameStart(QuicByteCount header_length,
                                         QuicByteCount payload_length) {
  DCHECK(VersionUsesHttp3(transport_version()));
  DCHECK(!qpack_decoded_headers_accumulator_);

  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnHeadersFrameReceived(id(),
                                                           payload_length);
  }

  if (trailers_decompressed_) {
    stream_delegate()->OnStreamError(
        QUIC_HTTP_INVALID_FRAME_SEQUENCE_ON_SPDY_STREAM,
        "HEADERS frame received after trailing HEADERS.");
    return false;
  }

  sequencer()->MarkConsumed(body_manager_.OnNonBody(header_length));

  qpack_decoded_headers_accumulator_ =
      std::make_unique<QpackDecodedHeadersAccumulator>(
          id(), spdy_session_->qpack_decoder(), this,
          spdy_session_->max_inbound_header_list_size());

  return true;
}

bool QuicSpdyStream::OnHeadersFramePayload(quiche::QuicheStringPiece payload) {
  DCHECK(VersionUsesHttp3(transport_version()));
  DCHECK(qpack_decoded_headers_accumulator_);

  // TODO(b/152518220): Save |payload_length| argument of OnHeadersFrameStart()
  // instead of accumulating payload length in |headers_payload_length_| or
  // |trailers_payload_length_|.
  if (headers_decompressed_) {
    trailers_payload_length_ += payload.length();
  } else {
    headers_payload_length_ += payload.length();
  }

  qpack_decoded_headers_accumulator_->Decode(payload);

  // |qpack_decoded_headers_accumulator_| is reset if an error is detected.
  if (!qpack_decoded_headers_accumulator_) {
    return false;
  }

  sequencer()->MarkConsumed(body_manager_.OnNonBody(payload.size()));
  return true;
}

bool QuicSpdyStream::OnHeadersFrameEnd() {
  DCHECK(VersionUsesHttp3(transport_version()));
  DCHECK(qpack_decoded_headers_accumulator_);

  qpack_decoded_headers_accumulator_->EndHeaderBlock();

  // If decoding is complete or an error is detected, then
  // |qpack_decoded_headers_accumulator_| is already reset.
  if (qpack_decoded_headers_accumulator_) {
    blocked_on_decoding_headers_ = true;
    return false;
  }

  return !sequencer()->IsClosed() && !reading_stopped();
}

bool QuicSpdyStream::OnPushPromiseFrameStart(QuicByteCount header_length) {
  DCHECK(VersionUsesHttp3(transport_version()));
  DCHECK(!qpack_decoded_headers_accumulator_);

  sequencer()->MarkConsumed(body_manager_.OnNonBody(header_length));

  return true;
}

bool QuicSpdyStream::OnPushPromiseFramePushId(
    PushId push_id,
    QuicByteCount push_id_length,
    QuicByteCount header_block_length) {
  DCHECK(VersionUsesHttp3(transport_version()));
  DCHECK(!qpack_decoded_headers_accumulator_);

  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnPushPromiseFrameReceived(
        id(), push_id, header_block_length);
  }

  // TODO(renjietang): Check max push id and handle errors.
  spdy_session_->OnPushPromise(id(), push_id);
  sequencer()->MarkConsumed(body_manager_.OnNonBody(push_id_length));

  qpack_decoded_headers_accumulator_ =
      std::make_unique<QpackDecodedHeadersAccumulator>(
          id(), spdy_session_->qpack_decoder(), this,
          spdy_session_->max_inbound_header_list_size());

  return true;
}

bool QuicSpdyStream::OnPushPromiseFramePayload(
    quiche::QuicheStringPiece payload) {
  spdy_session_->OnCompressedFrameSize(payload.length());
  return OnHeadersFramePayload(payload);
}

bool QuicSpdyStream::OnPushPromiseFrameEnd() {
  DCHECK(VersionUsesHttp3(transport_version()));

  return OnHeadersFrameEnd();
}

bool QuicSpdyStream::OnUnknownFrameStart(uint64_t frame_type,
                                         QuicByteCount header_length,
                                         QuicByteCount payload_length) {
  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnUnknownFrameReceived(id(), frame_type,
                                                           payload_length);
  }

  // Ignore unknown frames, but consume frame header.
  QUIC_DVLOG(1) << ENDPOINT << "Discarding " << header_length
                << " byte long frame header of frame of unknown type "
                << frame_type << ".";
  sequencer()->MarkConsumed(body_manager_.OnNonBody(header_length));
  return true;
}

bool QuicSpdyStream::OnUnknownFramePayload(quiche::QuicheStringPiece payload) {
  // Ignore unknown frames, but consume frame payload.
  QUIC_DVLOG(1) << ENDPOINT << "Discarding " << payload.size()
                << " bytes of payload of frame of unknown type.";
  sequencer()->MarkConsumed(body_manager_.OnNonBody(payload.size()));
  return true;
}

bool QuicSpdyStream::OnUnknownFrameEnd() {
  return true;
}

size_t QuicSpdyStream::WriteHeadersImpl(
    spdy::SpdyHeaderBlock header_block,
    bool fin,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  if (!VersionUsesHttp3(transport_version())) {
    return spdy_session_->WriteHeadersOnHeadersStream(
        id(), std::move(header_block), fin, precedence(),
        std::move(ack_listener));
  }

  // Encode header list.
  QuicByteCount encoder_stream_sent_byte_count;
  std::string encoded_headers =
      spdy_session_->qpack_encoder()->EncodeHeaderList(
          id(), header_block, &encoder_stream_sent_byte_count);

  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnHeadersFrameSent(id(), header_block);
  }

  // Write HEADERS frame.
  std::unique_ptr<char[]> headers_frame_header;
  const size_t headers_frame_header_length =
      HttpEncoder::SerializeHeadersFrameHeader(encoded_headers.size(),
                                               &headers_frame_header);
  unacked_frame_headers_offsets_.Add(
      send_buffer().stream_offset(),
      send_buffer().stream_offset() + headers_frame_header_length);

  QUIC_DLOG(INFO) << ENDPOINT << "Stream " << id()
                  << " is writing HEADERS frame header of length "
                  << headers_frame_header_length;
  WriteOrBufferData(quiche::QuicheStringPiece(headers_frame_header.get(),
                                              headers_frame_header_length),
                    /* fin = */ false, /* ack_listener = */ nullptr);

  QUIC_DLOG(INFO) << ENDPOINT << "Stream " << id()
                  << " is writing HEADERS frame payload of length "
                  << encoded_headers.length() << " with fin " << fin;
  WriteOrBufferData(encoded_headers, fin, nullptr);

  QuicSpdySession::LogHeaderCompressionRatioHistogram(
      /* using_qpack = */ true,
      /* is_sent = */ true,
      encoded_headers.size() + encoder_stream_sent_byte_count,
      header_block.TotalBytesUsed());

  return encoded_headers.size() + encoder_stream_sent_byte_count;
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
