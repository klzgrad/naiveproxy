// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_stream.h"

#include <limits>
#include <string>
#include <utility>

#include "net/third_party/quiche/src/quic/core/http/quic_spdy_session.h"
#include "net/third_party/quiche/src/quic/core/http/spdy_utils.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoded_headers_accumulator.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_decoder.h"
#include "net/third_party/quiche/src/quic/core/qpack/qpack_encoder.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/core/quic_write_blocked_list.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice_storage.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_string_piece.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_text_utils.h"
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
    stream_->session()->connection()->CloseConnection(
        QUIC_HTTP_DECODER_ERROR, "Http decoder internal error",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }

  void OnPriorityFrame(const PriorityFrame& frame) override {
    CloseConnectionOnWrongFrame("Priority");
  }

  void OnCancelPushFrame(const CancelPushFrame& frame) override {
    CloseConnectionOnWrongFrame("Cancel Push");
  }

  void OnMaxPushIdFrame(const MaxPushIdFrame& frame) override {
    CloseConnectionOnWrongFrame("Max Push Id");
  }

  void OnGoAwayFrame(const GoAwayFrame& frame) override {
    CloseConnectionOnWrongFrame("Goaway");
  }

  void OnSettingsFrameStart(Http3FrameLengths frame_lengths) override {
    CloseConnectionOnWrongFrame("Settings");
  }

  void OnSettingsFrame(const SettingsFrame& frame) override {
    CloseConnectionOnWrongFrame("Settings");
  }

  void OnDuplicatePushFrame(const DuplicatePushFrame& frame) override {
    CloseConnectionOnWrongFrame("Duplicate Push");
  }

  void OnDataFrameStart(Http3FrameLengths frame_lengths) override {
    stream_->OnDataFrameStart(frame_lengths);
  }

  void OnDataFramePayload(QuicStringPiece payload) override {
    DCHECK(!payload.empty());
    stream_->OnDataFramePayload(payload);
  }

  void OnDataFrameEnd() override { stream_->OnDataFrameEnd(); }

  void OnHeadersFrameStart(Http3FrameLengths frame_length) override {
    if (!VersionUsesQpack(
            stream_->session()->connection()->transport_version())) {
      CloseConnectionOnWrongFrame("Headers");
      return;
    }
    stream_->OnHeadersFrameStart(frame_length);
  }

  void OnHeadersFramePayload(QuicStringPiece payload) override {
    DCHECK(!payload.empty());
    if (!VersionUsesQpack(
            stream_->session()->connection()->transport_version())) {
      CloseConnectionOnWrongFrame("Headers");
      return;
    }
    stream_->OnHeadersFramePayload(payload);
  }

  void OnHeadersFrameEnd() override {
    if (!VersionUsesQpack(
            stream_->session()->connection()->transport_version())) {
      CloseConnectionOnWrongFrame("Headers");
      return;
    }
    stream_->OnHeadersFrameEnd();
  }

  void OnPushPromiseFrameStart(PushId push_id) override {
    CloseConnectionOnWrongFrame("Push Promise");
  }

  void OnPushPromiseFramePayload(QuicStringPiece payload) override {
    DCHECK(!payload.empty());
    CloseConnectionOnWrongFrame("Push Promise");
  }

  void OnPushPromiseFrameEnd() override {
    CloseConnectionOnWrongFrame("Push Promise");
  }

 private:
  void CloseConnectionOnWrongFrame(std::string frame_type) {
    stream_->session()->connection()->CloseConnection(
        QUIC_HTTP_DECODER_ERROR, frame_type + " frame received on data stream",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
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
      headers_decompressed_(false),
      headers_length_(0, 0),
      trailers_length_(0, 0),
      trailers_decompressed_(false),
      trailers_consumed_(false),
      http_decoder_visitor_(new HttpDecoderVisitor(this)),
      body_buffer_(sequencer()),
      ack_listener_(nullptr) {
  DCHECK(!QuicUtils::IsCryptoStreamId(
      spdy_session->connection()->transport_version(), id));
  // If headers are sent on the headers stream, then do not receive any
  // callbacks from the sequencer until headers are complete.
  if (!VersionUsesQpack(spdy_session_->connection()->transport_version())) {
    sequencer()->SetBlockedUntilFlush();
  }

  if (VersionHasDataFrameHeader(
          spdy_session_->connection()->transport_version())) {
    sequencer()->set_level_triggered(true);
  }
  decoder_.set_visitor(http_decoder_visitor_.get());
}

QuicSpdyStream::QuicSpdyStream(PendingStream pending,
                               QuicSpdySession* spdy_session,
                               StreamType type)
    : QuicStream(std::move(pending), type, /*is_static=*/false),
      spdy_session_(spdy_session),
      on_body_available_called_because_sequencer_is_closed_(false),
      visitor_(nullptr),
      headers_decompressed_(false),
      headers_length_(0, 0),
      trailers_length_(0, 0),
      trailers_decompressed_(false),
      trailers_consumed_(false),
      http_decoder_visitor_(new HttpDecoderVisitor(this)),
      body_buffer_(sequencer()),
      ack_listener_(nullptr) {
  DCHECK(!QuicUtils::IsCryptoStreamId(
      spdy_session->connection()->transport_version(), id()));
  // If headers are sent on the headers stream, then do not receive any
  // callbacks from the sequencer until headers are complete.
  if (!VersionUsesQpack(spdy_session_->connection()->transport_version())) {
    sequencer()->SetBlockedUntilFlush();
  }

  if (VersionHasDataFrameHeader(
          spdy_session_->connection()->transport_version())) {
    sequencer()->set_level_triggered(true);
  }
  decoder_.set_visitor(http_decoder_visitor_.get());
}

QuicSpdyStream::~QuicSpdyStream() {}

size_t QuicSpdyStream::WriteHeaders(
    SpdyHeaderBlock header_block,
    bool fin,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  QuicConnection::ScopedPacketFlusher flusher(
      spdy_session_->connection(), QuicConnection::SEND_ACK_IF_PENDING);
  // Send stream type for server push stream
  if (VersionHasStreamType(session()->connection()->transport_version()) &&
      type() == WRITE_UNIDIRECTIONAL && send_buffer().stream_offset() == 0) {
    char data[sizeof(kServerPushStream)];
    QuicDataWriter writer(QUIC_ARRAYSIZE(data), data);
    writer.WriteVarInt62(kServerPushStream);

    // Similar to frame headers, stream type byte shouldn't be exposed to upper
    // layer applications.
    unacked_frame_headers_offsets_.Add(0, writer.length());

    QUIC_LOG(INFO) << "Stream " << id() << " is writing type as server push";
    WriteOrBufferData(QuicStringPiece(writer.data(), writer.length()), false,
                      nullptr);
  }
  size_t bytes_written =
      WriteHeadersImpl(std::move(header_block), fin, std::move(ack_listener));
  if (!VersionUsesQpack(spdy_session_->connection()->transport_version()) &&
      fin) {
    // If HEADERS are sent on the headers stream, then |fin_sent_| needs to be
    // set and write side needs to be closed without actually sending a FIN on
    // this stream.
    // TODO(rch): Add test to ensure fin_sent_ is set whenever a fin is sent.
    set_fin_sent(true);
    CloseWriteSide();
  }
  return bytes_written;
}

void QuicSpdyStream::WriteOrBufferBody(QuicStringPiece data, bool fin) {
  if (!VersionHasDataFrameHeader(
          spdy_session_->connection()->transport_version()) ||
      data.length() == 0) {
    WriteOrBufferData(data, fin, nullptr);
    return;
  }
  QuicConnection::ScopedPacketFlusher flusher(
      spdy_session_->connection(), QuicConnection::SEND_ACK_IF_PENDING);

  // Write frame header.
  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      encoder_.SerializeDataFrameHeader(data.length(), &buffer);
  unacked_frame_headers_offsets_.Add(
      send_buffer().stream_offset(),
      send_buffer().stream_offset() + header_length);
  QUIC_DLOG(INFO) << "Stream " << id()
                  << " is writing DATA frame header of length "
                  << header_length;
  WriteOrBufferData(QuicStringPiece(buffer.get(), header_length), false,
                    nullptr);

  // Write body.
  QUIC_DLOG(INFO) << "Stream " << id()
                  << " is writing DATA frame payload of length "
                  << data.length();
  WriteOrBufferData(data, fin, nullptr);
}

size_t QuicSpdyStream::WriteTrailers(
    SpdyHeaderBlock trailer_block,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  if (fin_sent()) {
    QUIC_BUG << "Trailers cannot be sent after a FIN, on stream " << id();
    return 0;
  }

  if (!VersionUsesQpack(spdy_session_->connection()->transport_version())) {
    // The header block must contain the final offset for this stream, as the
    // trailers may be processed out of order at the peer.
    const QuicStreamOffset final_offset =
        stream_bytes_written() + BufferedDataBytes();
    QUIC_DLOG(INFO) << "Inserting trailer: (" << kFinalOffsetHeaderKey << ", "
                    << final_offset << ")";
    trailer_block.insert(std::make_pair(
        kFinalOffsetHeaderKey, QuicTextUtils::Uint64ToString(final_offset)));
  }

  // Write the trailing headers with a FIN, and close stream for writing:
  // trailers are the last thing to be sent on a stream.
  const bool kFin = true;
  size_t bytes_written =
      WriteHeadersImpl(std::move(trailer_block), kFin, std::move(ack_listener));

  // If trailers are sent on the headers stream, then |fin_sent_| needs to be
  // set without actually sending a FIN on this stream.
  if (!VersionUsesQpack(spdy_session_->connection()->transport_version())) {
    set_fin_sent(kFin);

    // Also, write side of this stream needs to be closed.  However, only do
    // this if there is no more buffered data, otherwise it will never be sent.
    if (BufferedDataBytes() == 0) {
      CloseWriteSide();
    }
  }

  return bytes_written;
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
  if (!VersionHasDataFrameHeader(
          spdy_session_->connection()->transport_version()) ||
      slices.empty()) {
    return WriteMemSlices(slices, fin);
  }

  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      encoder_.SerializeDataFrameHeader(slices.total_length(), &buffer);
  if (!CanWriteNewDataAfterData(header_length)) {
    return {0, false};
  }

  QuicConnection::ScopedPacketFlusher flusher(
      spdy_session_->connection(), QuicConnection::SEND_ACK_IF_PENDING);

  // Write frame header.
  struct iovec header_iov = {static_cast<void*>(buffer.get()), header_length};
  QuicMemSliceStorage storage(
      &header_iov, 1,
      spdy_session_->connection()->helper()->GetStreamSendBufferAllocator(),
      GetQuicFlag(FLAGS_quic_send_buffer_max_data_slice_size));
  unacked_frame_headers_offsets_.Add(
      send_buffer().stream_offset(),
      send_buffer().stream_offset() + header_length);
  QUIC_DLOG(INFO) << "Stream " << id()
                  << " is writing DATA frame header of length "
                  << header_length;
  WriteMemSlices(storage.ToSpan(), false);

  // Write body.
  QUIC_DLOG(INFO) << "Stream " << id()
                  << " is writing DATA frame payload of length "
                  << slices.total_length();
  return WriteMemSlices(slices, fin);
}

size_t QuicSpdyStream::Readv(const struct iovec* iov, size_t iov_len) {
  DCHECK(FinishedReadingHeaders());
  if (!VersionHasDataFrameHeader(
          spdy_session_->connection()->transport_version())) {
    return sequencer()->Readv(iov, iov_len);
  }
  return body_buffer_.ReadBody(iov, iov_len);
}

int QuicSpdyStream::GetReadableRegions(iovec* iov, size_t iov_len) const {
  DCHECK(FinishedReadingHeaders());
  if (!VersionHasDataFrameHeader(
          spdy_session_->connection()->transport_version())) {
    return sequencer()->GetReadableRegions(iov, iov_len);
  }
  return body_buffer_.PeekBody(iov, iov_len);
}

void QuicSpdyStream::MarkConsumed(size_t num_bytes) {
  DCHECK(FinishedReadingHeaders());
  if (!VersionHasDataFrameHeader(
          spdy_session_->connection()->transport_version())) {
    sequencer()->MarkConsumed(num_bytes);
    return;
  }
  body_buffer_.MarkBodyConsumed(num_bytes);
}

bool QuicSpdyStream::IsDoneReading() const {
  bool done_reading_headers = FinishedReadingHeaders();
  bool done_reading_body = sequencer()->IsClosed();
  bool done_reading_trailers = FinishedReadingTrailers();
  return done_reading_headers && done_reading_body && done_reading_trailers;
}

bool QuicSpdyStream::HasBytesToRead() const {
  if (!VersionHasDataFrameHeader(
          spdy_session_->connection()->transport_version())) {
    return sequencer()->HasBytesToRead();
  }
  return body_buffer_.HasBytesToRead();
}

void QuicSpdyStream::MarkTrailersConsumed() {
  if (VersionUsesQpack(spdy_session_->connection()->transport_version()) &&
      !reading_stopped()) {
    const QuicByteCount trailers_total_length =
        trailers_length_.header_length + trailers_length_.payload_length;
    if (trailers_total_length > 0) {
      sequencer()->MarkConsumed(trailers_total_length);
    }
  }

  trailers_consumed_ = true;
}

uint64_t QuicSpdyStream::total_body_bytes_read() const {
  if (VersionHasDataFrameHeader(
          spdy_session_->connection()->transport_version())) {
    return body_buffer_.total_body_bytes_received();
  }
  return sequencer()->NumBytesConsumed();
}

void QuicSpdyStream::ConsumeHeaderList() {
  header_list_.Clear();

  if (!VersionUsesQpack(spdy_session_->connection()->transport_version())) {
    if (FinishedReadingHeaders()) {
      sequencer()->SetUnblocked();
    }
    return;
  }

  if (!reading_stopped()) {
    const QuicByteCount headers_total_length =
        headers_length_.header_length + headers_length_.payload_length;
    if (headers_total_length > 0) {
      sequencer()->MarkConsumed(headers_total_length);
    }
  }

  if (!FinishedReadingHeaders()) {
    return;
  }

  if (body_buffer_.HasBytesToRead()) {
    OnBodyAvailable();
    return;
  }

  if (sequencer()->IsClosed() &&
      !on_body_available_called_because_sequencer_is_closed_) {
    on_body_available_called_because_sequencer_is_closed_ = true;
    OnBodyAvailable();
  }
}

void QuicSpdyStream::OnStreamHeadersPriority(SpdyPriority priority) {
  DCHECK_EQ(Perspective::IS_SERVER, session()->connection()->perspective());
  SetPriority(priority);
}

void QuicSpdyStream::OnStreamHeaderList(bool fin,
                                        size_t frame_len,
                                        const QuicHeaderList& header_list) {
  // The headers list avoid infinite buffering by clearing the headers list
  // if the current headers are too large. So if the list is empty here
  // then the headers list must have been too large, and the stream should
  // be reset.
  // TODO(rch): Use an explicit "headers too large" signal. An empty header list
  // might be acceptable if it corresponds to a trailing header frame.
  if (header_list.empty()) {
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

void QuicSpdyStream::OnHeadersTooLarge() {
  if (VersionUsesQpack(spdy_session_->connection()->transport_version())) {
    // TODO(124216424): Use HTTP_EXCESSIVE_LOAD error code.
    std::string error_message =
        QuicStrCat("Too large headers received on stream ", id());
    CloseConnectionWithDetails(QUIC_HEADERS_STREAM_DATA_DECOMPRESS_FAILURE,
                               error_message);
  } else {
    Reset(QUIC_HEADERS_TOO_LARGE);
  }
}

void QuicSpdyStream::OnInitialHeadersComplete(
    bool fin,
    size_t /*frame_len*/,
    const QuicHeaderList& header_list) {
  headers_decompressed_ = true;
  header_list_ = header_list;

  if (VersionUsesQpack(spdy_session_->connection()->transport_version())) {
    if (fin) {
      OnStreamFrame(
          QuicStreamFrame(id(), /* fin = */ true,
                          flow_controller()->highest_received_byte_offset(),
                          QuicStringPiece()));
    }
    return;
  }

  if (fin) {
    OnStreamFrame(
        QuicStreamFrame(id(), fin, /* offset = */ 0, QuicStringPiece()));
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
  session()->connection()->CloseConnection(
      QUIC_INVALID_HEADERS_STREAM_DATA, "Promise headers received by server",
      ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
}

void QuicSpdyStream::OnTrailingHeadersComplete(
    bool fin,
    size_t /*frame_len*/,
    const QuicHeaderList& header_list) {
  DCHECK(!trailers_decompressed_);
  if ((VersionUsesQpack(spdy_session_->connection()->transport_version()) &&
       sequencer()->IsClosed()) ||
      (!VersionUsesQpack(spdy_session_->connection()->transport_version()) &&
       fin_received())) {
    QUIC_DLOG(INFO) << "Received Trailers after FIN, on stream: " << id();
    session()->connection()->CloseConnection(
        QUIC_INVALID_HEADERS_STREAM_DATA, "Trailers after fin",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  if (!VersionUsesQpack(spdy_session_->connection()->transport_version()) &&
      !fin) {
    QUIC_DLOG(INFO) << "Trailers must have FIN set, on stream: " << id();
    session()->connection()->CloseConnection(
        QUIC_INVALID_HEADERS_STREAM_DATA, "Fin missing from trailers",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  size_t final_byte_offset = 0;
  const bool expect_final_byte_offset =
      !VersionUsesQpack(spdy_session_->connection()->transport_version());
  if (!SpdyUtils::CopyAndValidateTrailers(header_list, expect_final_byte_offset,
                                          &final_byte_offset,
                                          &received_trailers_)) {
    QUIC_DLOG(ERROR) << "Trailers for stream " << id() << " are malformed.";
    session()->connection()->CloseConnection(
        QUIC_INVALID_HEADERS_STREAM_DATA, "Trailers are malformed",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }
  trailers_decompressed_ = true;
  const QuicStreamOffset offset =
      VersionUsesQpack(spdy_session_->connection()->transport_version())
          ? flow_controller()->highest_received_byte_offset()
          : final_byte_offset;
  OnStreamFrame(
      QuicStreamFrame(id(), /* fin = */ true, offset, QuicStringPiece()));
}

void QuicSpdyStream::OnPriorityFrame(SpdyPriority priority) {
  DCHECK_EQ(Perspective::IS_SERVER, session()->connection()->perspective());
  SetPriority(priority);
}

void QuicSpdyStream::OnStreamReset(const QuicRstStreamFrame& frame) {
  if (frame.error_code != QUIC_STREAM_NO_ERROR) {
    QuicStream::OnStreamReset(frame);
    return;
  }
  QUIC_DVLOG(1) << "Received QUIC_STREAM_NO_ERROR, not discarding response";
  set_rst_received(true);
  MaybeIncreaseHighestReceivedOffset(frame.byte_offset);
  set_stream_error(frame.error_code);
  CloseWriteSide();
}

void QuicSpdyStream::OnDataAvailable() {
  if (!VersionUsesQpack(spdy_session_->connection()->transport_version())) {
    // Sequencer must be blocked until headers are consumed.
    DCHECK(FinishedReadingHeaders());
  }

  if (!VersionHasDataFrameHeader(
          session()->connection()->transport_version())) {
    OnBodyAvailable();
    return;
  }

  iovec iov;
  while (!reading_stopped() && sequencer()->PrefetchNextRegion(&iov)) {
    decoder_.ProcessInput(reinterpret_cast<const char*>(iov.iov_base),
                          iov.iov_len);
  }

  // Do not call OnBodyAvailable() until headers are consumed.
  if (!FinishedReadingHeaders()) {
    return;
  }

  if (body_buffer_.HasBytesToRead()) {
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

bool QuicSpdyStream::ParseHeaderStatusCode(const SpdyHeaderBlock& header,
                                           int* status_code) const {
  SpdyHeaderBlock::const_iterator it = header.find(spdy::kHttp2StatusHeader);
  if (it == header.end()) {
    return false;
  }
  const QuicStringPiece status(it->second);
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
  return QuicTextUtils::StringToInt(status, status_code);
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

void QuicSpdyStream::OnDataFrameStart(Http3FrameLengths frame_lengths) {
  DCHECK(
      VersionHasDataFrameHeader(session()->connection()->transport_version()));

  body_buffer_.OnDataHeader(frame_lengths);
}

void QuicSpdyStream::OnDataFramePayload(QuicStringPiece payload) {
  DCHECK(
      VersionHasDataFrameHeader(session()->connection()->transport_version()));

  body_buffer_.OnDataPayload(payload);
}

void QuicSpdyStream::OnDataFrameEnd() {
  DCHECK(
      VersionHasDataFrameHeader(session()->connection()->transport_version()));
  QUIC_DVLOG(1) << "Reaches the end of a data frame. Total bytes received are "
                << body_buffer_.total_body_bytes_received();
}

bool QuicSpdyStream::OnStreamFrameAcked(QuicStreamOffset offset,
                                        QuicByteCount data_length,
                                        bool fin_acked,
                                        QuicTime::Delta ack_delay_time,
                                        QuicByteCount* newly_acked_length) {
  const bool new_data_acked = QuicStream::OnStreamFrameAcked(
      offset, data_length, fin_acked, ack_delay_time, newly_acked_length);

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

void QuicSpdyStream::OnHeadersFrameStart(Http3FrameLengths frame_length) {
  DCHECK(VersionUsesQpack(spdy_session_->connection()->transport_version()));
  DCHECK(!qpack_decoded_headers_accumulator_);

  if (headers_decompressed_) {
    trailers_length_ = frame_length;
  } else {
    headers_length_ = frame_length;
  }

  qpack_decoded_headers_accumulator_ =
      QuicMakeUnique<QpackDecodedHeadersAccumulator>(
          id(), spdy_session_->qpack_decoder(),
          spdy_session_->max_inbound_header_list_size());
}

void QuicSpdyStream::OnHeadersFramePayload(QuicStringPiece payload) {
  DCHECK(VersionUsesQpack(spdy_session_->connection()->transport_version()));

  if (!qpack_decoded_headers_accumulator_->Decode(payload)) {
    // TODO(124216424): Use HTTP_QPACK_DECOMPRESSION_FAILED error code.
    std::string error_message =
        QuicStrCat("Error decompressing header block on stream ", id(), ": ",
                   qpack_decoded_headers_accumulator_->error_message());
    CloseConnectionWithDetails(QUIC_DECOMPRESSION_FAILURE, error_message);
    return;
  }
}

void QuicSpdyStream::OnHeadersFrameEnd() {
  DCHECK(VersionUsesQpack(spdy_session_->connection()->transport_version()));

  if (!qpack_decoded_headers_accumulator_->EndHeaderBlock()) {
    // TODO(124216424): Use HTTP_QPACK_DECOMPRESSION_FAILED error code.
    std::string error_message =
        QuicStrCat("Error decompressing header block on stream ", id(), ": ",
                   qpack_decoded_headers_accumulator_->error_message());
    CloseConnectionWithDetails(QUIC_DECOMPRESSION_FAILURE, error_message);
    return;
  }

  const QuicByteCount frame_length = headers_decompressed_
                                         ? trailers_length_.payload_length
                                         : headers_length_.payload_length;
  OnStreamHeaderList(/* fin = */ false, frame_length,
                     qpack_decoded_headers_accumulator_->quic_header_list());

  qpack_decoded_headers_accumulator_.reset();
}

size_t QuicSpdyStream::WriteHeadersImpl(
    spdy::SpdyHeaderBlock header_block,
    bool fin,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  if (!VersionUsesQpack(spdy_session_->connection()->transport_version())) {
    return spdy_session_->WriteHeadersOnHeadersStream(
        id(), std::move(header_block), fin, priority(),
        std::move(ack_listener));
  }

  // Encode header list.
  auto progressive_encoder = spdy_session_->qpack_encoder()->EncodeHeaderList(
      /* stream_id = */ id(), &header_block);
  std::string encoded_headers;
  while (progressive_encoder->HasNext()) {
    progressive_encoder->Next(
        /* max_encoded_bytes = */ std::numeric_limits<size_t>::max(),
        &encoded_headers);
  }

  // Write HEADERS frame.
  std::unique_ptr<char[]> headers_frame_header;
  const size_t headers_frame_header_length =
      encoder_.SerializeHeadersFrameHeader(encoded_headers.size(),
                                           &headers_frame_header);
  unacked_frame_headers_offsets_.Add(
      send_buffer().stream_offset(),
      send_buffer().stream_offset() + headers_frame_header_length);

  QUIC_DLOG(INFO) << "Stream " << id()
                  << " is writing HEADERS frame header of length "
                  << headers_frame_header_length;
  WriteOrBufferData(
      QuicStringPiece(headers_frame_header.get(), headers_frame_header_length),
      /* fin = */ false, /* ack_listener = */ nullptr);

  QUIC_DLOG(INFO) << "Stream " << id()
                  << " is writing HEADERS frame payload of length "
                  << encoded_headers.length();
  WriteOrBufferData(encoded_headers, fin, nullptr);

  return encoded_headers.size();
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
