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
#include "net/third_party/quiche/src/quic/platform/api/quic_arraysize.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_bug_tracker.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flag_utils.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_mem_slice_storage.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
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

  void OnError(HttpDecoder* /*decoder*/) override {
    stream_->session()->connection()->CloseConnection(
        QUIC_HTTP_DECODER_ERROR, "Http decoder internal error",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
  }

  bool OnPriorityFrameStart(QuicByteCount /*header_length*/) override {
    CloseConnectionOnWrongFrame("Priority");
    return false;
  }

  bool OnPriorityFrame(const PriorityFrame& /*frame*/) override {
    CloseConnectionOnWrongFrame("Priority");
    return false;
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

  bool OnDuplicatePushFrame(const DuplicatePushFrame& /*frame*/) override {
    // TODO(b/137554973): Consume frame.
    CloseConnectionOnWrongFrame("Duplicate Push");
    return false;
  }

  bool OnDataFrameStart(QuicByteCount header_length) override {
    return stream_->OnDataFrameStart(header_length);
  }

  bool OnDataFramePayload(QuicStringPiece payload) override {
    DCHECK(!payload.empty());
    return stream_->OnDataFramePayload(payload);
  }

  bool OnDataFrameEnd() override { return stream_->OnDataFrameEnd(); }

  bool OnHeadersFrameStart(QuicByteCount header_length) override {
    if (!VersionUsesQpack(stream_->transport_version())) {
      CloseConnectionOnWrongFrame("Headers");
      return false;
    }
    return stream_->OnHeadersFrameStart(header_length);
  }

  bool OnHeadersFramePayload(QuicStringPiece payload) override {
    DCHECK(!payload.empty());
    if (!VersionUsesQpack(stream_->transport_version())) {
      CloseConnectionOnWrongFrame("Headers");
      return false;
    }
    return stream_->OnHeadersFramePayload(payload);
  }

  bool OnHeadersFrameEnd() override {
    if (!VersionUsesQpack(stream_->transport_version())) {
      CloseConnectionOnWrongFrame("Headers");
      return false;
    }
    return stream_->OnHeadersFrameEnd();
  }

  bool OnPushPromiseFrameStart(PushId push_id,
                               QuicByteCount header_length,
                               QuicByteCount push_id_length) override {
    if (!VersionHasStreamType(stream_->transport_version())) {
      CloseConnectionOnWrongFrame("Push Promise");
      return false;
    }
    return stream_->OnPushPromiseFrameStart(push_id, header_length,
                                            push_id_length);
  }

  bool OnPushPromiseFramePayload(QuicStringPiece payload) override {
    DCHECK(!payload.empty());
    if (!VersionUsesQpack(stream_->transport_version())) {
      CloseConnectionOnWrongFrame("Push Promise");
      return false;
    }
    return stream_->OnPushPromiseFramePayload(payload);
  }

  bool OnPushPromiseFrameEnd() override {
    if (!VersionUsesQpack(stream_->transport_version())) {
      CloseConnectionOnWrongFrame("Push Promise");
      return false;
    }
    return stream_->OnPushPromiseFrameEnd();
  }

  bool OnUnknownFrameStart(uint64_t frame_type,
                           QuicByteCount header_length) override {
    return stream_->OnUnknownFrameStart(frame_type, header_length);
  }

  bool OnUnknownFramePayload(QuicStringPiece payload) override {
    return stream_->OnUnknownFramePayload(payload);
  }

  bool OnUnknownFrameEnd() override { return stream_->OnUnknownFrameEnd(); }

 private:
  void CloseConnectionOnWrongFrame(QuicStringPiece frame_type) {
    stream_->session()->connection()->CloseConnection(
        QUIC_HTTP_DECODER_ERROR,
        QuicStrCat(frame_type, " frame received on data stream"),
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
      blocked_on_decoding_headers_(false),
      headers_decompressed_(false),
      headers_payload_length_(0),
      trailers_payload_length_(0),
      trailers_decompressed_(false),
      trailers_consumed_(false),
      priority_sent_(false),
      http_decoder_visitor_(QuicMakeUnique<HttpDecoderVisitor>(this)),
      decoder_(http_decoder_visitor_.get()),
      sequencer_offset_(0),
      is_decoder_processing_input_(false),
      ack_listener_(nullptr) {
  DCHECK_EQ(session()->connection(), spdy_session->connection());
  DCHECK_EQ(transport_version(),
            spdy_session->connection()->transport_version());
  DCHECK(!QuicUtils::IsCryptoStreamId(transport_version(), id));
  DCHECK_EQ(0u, sequencer()->NumBytesConsumed());
  // If headers are sent on the headers stream, then do not receive any
  // callbacks from the sequencer until headers are complete.
  if (!VersionUsesQpack(transport_version())) {
    sequencer()->SetBlockedUntilFlush();
  }

  if (VersionHasDataFrameHeader(transport_version())) {
    sequencer()->set_level_triggered(true);
  }
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
      headers_payload_length_(0),
      trailers_payload_length_(0),
      trailers_decompressed_(false),
      trailers_consumed_(false),
      priority_sent_(false),
      http_decoder_visitor_(QuicMakeUnique<HttpDecoderVisitor>(this)),
      decoder_(http_decoder_visitor_.get()),
      sequencer_offset_(sequencer()->NumBytesConsumed()),
      is_decoder_processing_input_(false),
      ack_listener_(nullptr) {
  DCHECK_EQ(session()->connection(), spdy_session->connection());
  DCHECK_EQ(transport_version(),
            spdy_session->connection()->transport_version());
  DCHECK(!QuicUtils::IsCryptoStreamId(transport_version(), id()));
  // If headers are sent on the headers stream, then do not receive any
  // callbacks from the sequencer until headers are complete.
  if (!VersionUsesQpack(transport_version())) {
    sequencer()->SetBlockedUntilFlush();
  }

  if (VersionHasDataFrameHeader(transport_version())) {
    sequencer()->set_level_triggered(true);
  }
}

QuicSpdyStream::~QuicSpdyStream() {}

size_t QuicSpdyStream::WriteHeaders(
    SpdyHeaderBlock header_block,
    bool fin,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  QuicConnection::ScopedPacketFlusher flusher(spdy_session_->connection());
  // Send stream type for server push stream
  if (VersionHasStreamType(transport_version()) &&
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
  if (!VersionUsesQpack(transport_version()) && fin) {
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
  if (!VersionHasDataFrameHeader(transport_version()) || data.length() == 0) {
    WriteOrBufferData(data, fin, nullptr);
    return;
  }
  QuicConnection::ScopedPacketFlusher flusher(spdy_session_->connection());

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

  if (!VersionUsesQpack(transport_version())) {
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
  if (!VersionUsesQpack(transport_version())) {
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
  DCHECK(VersionUsesQpack(transport_version()));
  std::unique_ptr<char[]> push_promise_frame_with_id;
  const size_t push_promise_frame_length =
      encoder_.SerializePushPromiseFrameWithOnlyPushId(
          frame, &push_promise_frame_with_id);

  unacked_frame_headers_offsets_.Add(send_buffer().stream_offset(),
                                     send_buffer().stream_offset() +
                                         push_promise_frame_length +
                                         frame.headers.length());

  // Write Push Promise frame header and push id.
  QUIC_DLOG(INFO) << "Stream " << id()
                  << " is writing Push Promise frame header of length "
                  << push_promise_frame_length << " , with promised id "
                  << frame.push_id;
  WriteOrBufferData(QuicStringPiece(push_promise_frame_with_id.get(),
                                    push_promise_frame_length),
                    /* fin = */ false, /* ack_listener = */ nullptr);

  // Write response headers.
  QUIC_DLOG(INFO) << "Stream " << id()
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
  if (!VersionHasDataFrameHeader(transport_version()) || slices.empty()) {
    return WriteMemSlices(slices, fin);
  }

  std::unique_ptr<char[]> buffer;
  QuicByteCount header_length =
      encoder_.SerializeDataFrameHeader(slices.total_length(), &buffer);
  if (!CanWriteNewDataAfterData(header_length)) {
    return {0, false};
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
  if (!VersionHasDataFrameHeader(transport_version())) {
    return sequencer()->Readv(iov, iov_len);
  }
  size_t bytes_read = 0;
  sequencer()->MarkConsumed(body_manager_.ReadBody(iov, iov_len, &bytes_read));

  return bytes_read;
}

int QuicSpdyStream::GetReadableRegions(iovec* iov, size_t iov_len) const {
  DCHECK(FinishedReadingHeaders());
  if (!VersionHasDataFrameHeader(transport_version())) {
    return sequencer()->GetReadableRegions(iov, iov_len);
  }
  return body_manager_.PeekBody(iov, iov_len);
}

void QuicSpdyStream::MarkConsumed(size_t num_bytes) {
  DCHECK(FinishedReadingHeaders());
  if (!VersionHasDataFrameHeader(transport_version())) {
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
  if (!VersionHasDataFrameHeader(transport_version())) {
    return sequencer()->HasBytesToRead();
  }
  return body_manager_.HasBytesToRead();
}

void QuicSpdyStream::MarkTrailersConsumed() {
  trailers_consumed_ = true;
}

uint64_t QuicSpdyStream::total_body_bytes_read() const {
  if (VersionHasDataFrameHeader(transport_version())) {
    return body_manager_.total_body_bytes_received();
  }
  return sequencer()->NumBytesConsumed();
}

void QuicSpdyStream::ConsumeHeaderList() {
  header_list_.Clear();

  if (!FinishedReadingHeaders()) {
    return;
  }

  if (!VersionUsesQpack(transport_version())) {
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

void QuicSpdyStream::OnHeadersDecoded(QuicHeaderList headers) {
  blocked_on_decoding_headers_ = false;
  ProcessDecodedHeaders(headers);
  // Continue decoding HTTP/3 frames.
  OnDataAvailable();
}

void QuicSpdyStream::OnHeaderDecodingError() {
  // TODO(b/124216424): Use HTTP_EXCESSIVE_LOAD or
  // HTTP_QPACK_DECOMPRESSION_FAILED error code as indicated by
  // |qpack_decoded_headers_accumulator_|.
  std::string error_message = QuicStrCat(
      "Error during async decoding of ",
      headers_decompressed_ ? "trailers" : "headers", " on stream ", id(), ": ",
      qpack_decoded_headers_accumulator_->error_message());
  CloseConnectionWithDetails(QUIC_DECOMPRESSION_FAILURE, error_message);
}

void QuicSpdyStream::OnHeadersTooLarge() {
  if (VersionUsesQpack(transport_version())) {
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
  // TODO(b/134706391): remove |fin| argument.
  headers_decompressed_ = true;
  header_list_ = header_list;

  if (VersionUsesQpack(transport_version())) {
    if (fin) {
      OnStreamFrame(
          QuicStreamFrame(id(), /* fin = */ true,
                          flow_controller()->highest_received_byte_offset(),
                          QuicStringPiece()));
    }
    return;
  }

  if (fin && !rst_sent()) {
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
  // TODO(b/134706391): remove |fin| argument.
  DCHECK(!trailers_decompressed_);
  if (!VersionUsesQpack(transport_version()) && fin_received()) {
    QUIC_DLOG(INFO) << "Received Trailers after FIN, on stream: " << id();
    session()->connection()->CloseConnection(
        QUIC_INVALID_HEADERS_STREAM_DATA, "Trailers after fin",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  if (!VersionUsesQpack(transport_version()) && !fin) {
    QUIC_DLOG(INFO) << "Trailers must have FIN set, on stream: " << id();
    session()->connection()->CloseConnection(
        QUIC_INVALID_HEADERS_STREAM_DATA, "Fin missing from trailers",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return;
  }

  size_t final_byte_offset = 0;
  const bool expect_final_byte_offset = !VersionUsesQpack(transport_version());
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
  if (fin) {
    const QuicStreamOffset offset =
        VersionUsesQpack(transport_version())
            ? flow_controller()->highest_received_byte_offset()
            : final_byte_offset;
    OnStreamFrame(QuicStreamFrame(id(), fin, offset, QuicStringPiece()));
  }
}

void QuicSpdyStream::OnPriorityFrame(
    const spdy::SpdyStreamPrecedence& precedence) {
  DCHECK_EQ(Perspective::IS_SERVER, session()->connection()->perspective());
  SetPriority(precedence);
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
  if (!VersionUsesQpack(transport_version())) {
    // Sequencer must be blocked until headers are consumed.
    DCHECK(FinishedReadingHeaders());
  }

  if (!VersionHasDataFrameHeader(transport_version())) {
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

bool QuicSpdyStream::OnDataFrameStart(QuicByteCount header_length) {
  DCHECK(VersionHasDataFrameHeader(transport_version()));
  if (!headers_decompressed_ || trailers_decompressed_) {
    // TODO(b/124216424): Change error code to HTTP_UNEXPECTED_FRAME.
    session()->connection()->CloseConnection(
        QUIC_INVALID_HEADERS_STREAM_DATA, "Unexpected DATA frame received.",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  sequencer()->MarkConsumed(body_manager_.OnNonBody(header_length));

  return true;
}

bool QuicSpdyStream::OnDataFramePayload(QuicStringPiece payload) {
  DCHECK(VersionHasDataFrameHeader(transport_version()));

  body_manager_.OnBody(payload);

  return true;
}

bool QuicSpdyStream::OnDataFrameEnd() {
  DCHECK(VersionHasDataFrameHeader(transport_version()));
  QUIC_DVLOG(1) << "Reaches the end of a data frame. Total bytes received are "
                << body_manager_.total_body_bytes_received();
  return true;
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

bool QuicSpdyStream::OnHeadersFrameStart(QuicByteCount header_length) {
  DCHECK(VersionUsesQpack(transport_version()));
  DCHECK(!qpack_decoded_headers_accumulator_);

  if (trailers_decompressed_) {
    // TODO(b/124216424): Change error code to HTTP_UNEXPECTED_FRAME.
    session()->connection()->CloseConnection(
        QUIC_INVALID_HEADERS_STREAM_DATA,
        "HEADERS frame received after trailing HEADERS.",
        ConnectionCloseBehavior::SEND_CONNECTION_CLOSE_PACKET);
    return false;
  }

  sequencer()->MarkConsumed(body_manager_.OnNonBody(header_length));

  qpack_decoded_headers_accumulator_ =
      QuicMakeUnique<QpackDecodedHeadersAccumulator>(
          id(), spdy_session_->qpack_decoder(), this,
          spdy_session_->max_inbound_header_list_size());

  return true;
}

bool QuicSpdyStream::OnHeadersFramePayload(QuicStringPiece payload) {
  DCHECK(VersionUsesQpack(transport_version()));

  if (headers_decompressed_) {
    trailers_payload_length_ += payload.length();
  } else {
    headers_payload_length_ += payload.length();
  }

  const bool success = qpack_decoded_headers_accumulator_->Decode(payload);

  sequencer()->MarkConsumed(body_manager_.OnNonBody(payload.size()));

  if (!success) {
    // TODO(124216424): Use HTTP_QPACK_DECOMPRESSION_FAILED error code.
    std::string error_message =
        QuicStrCat("Error decompressing header block on stream ", id(), ": ",
                   qpack_decoded_headers_accumulator_->error_message());
    CloseConnectionWithDetails(QUIC_DECOMPRESSION_FAILURE, error_message);
    return false;
  }
  return true;
}

bool QuicSpdyStream::OnHeadersFrameEnd() {
  DCHECK(VersionUsesQpack(transport_version()));

  auto result = qpack_decoded_headers_accumulator_->EndHeaderBlock();

  if (result == QpackDecodedHeadersAccumulator::Status::kError) {
    // TODO(124216424): Use HTTP_QPACK_DECOMPRESSION_FAILED error code.
    std::string error_message =
        QuicStrCat("Error decompressing header block on stream ", id(), ": ",
                   qpack_decoded_headers_accumulator_->error_message());
    CloseConnectionWithDetails(QUIC_DECOMPRESSION_FAILURE, error_message);
    return false;
  }

  if (result == QpackDecodedHeadersAccumulator::Status::kBlocked) {
    blocked_on_decoding_headers_ = true;
    return false;
  }

  DCHECK(result == QpackDecodedHeadersAccumulator::Status::kSuccess);

  ProcessDecodedHeaders(qpack_decoded_headers_accumulator_->quic_header_list());
  return !sequencer()->IsClosed() && !reading_stopped();
}

bool QuicSpdyStream::OnPushPromiseFrameStart(PushId push_id,
                                             QuicByteCount header_length,
                                             QuicByteCount push_id_length) {
  DCHECK(VersionHasStreamType(transport_version()));
  DCHECK(!qpack_decoded_headers_accumulator_);

  // TODO(renjietang): Check max push id and handle errors.
  spdy_session_->OnPushPromise(id(), push_id);
  sequencer()->MarkConsumed(
      body_manager_.OnNonBody(header_length + push_id_length));

  qpack_decoded_headers_accumulator_ =
      QuicMakeUnique<QpackDecodedHeadersAccumulator>(
          id(), spdy_session_->qpack_decoder(), this,
          spdy_session_->max_inbound_header_list_size());

  return true;
}

bool QuicSpdyStream::OnPushPromiseFramePayload(QuicStringPiece payload) {
  spdy_session_->OnCompressedFrameSize(payload.length());
  return OnHeadersFramePayload(payload);
}

bool QuicSpdyStream::OnPushPromiseFrameEnd() {
  DCHECK(VersionUsesQpack(transport_version()));

  return OnHeadersFrameEnd();
}

bool QuicSpdyStream::OnUnknownFrameStart(uint64_t frame_type,
                                         QuicByteCount header_length) {
  // Ignore unknown frames, but consume frame header.
  QUIC_DVLOG(1) << "Discarding " << header_length
                << " byte long frame header of frame of unknown type "
                << frame_type << ".";
  sequencer()->MarkConsumed(body_manager_.OnNonBody(header_length));
  return true;
}

bool QuicSpdyStream::OnUnknownFramePayload(QuicStringPiece payload) {
  // Ignore unknown frames, but consume frame payload.
  QUIC_DVLOG(1) << "Discarding " << payload.size()
                << " bytes of payload of frame of unknown type.";
  sequencer()->MarkConsumed(body_manager_.OnNonBody(payload.size()));
  return true;
}

bool QuicSpdyStream::OnUnknownFrameEnd() {
  return true;
}

void QuicSpdyStream::ProcessDecodedHeaders(const QuicHeaderList& headers) {
  if (spdy_session_->promised_stream_id() ==
      QuicUtils::GetInvalidStreamId(
          session()->connection()->transport_version())) {
    const QuicByteCount frame_length = headers_decompressed_
                                           ? trailers_payload_length_
                                           : headers_payload_length_;
    OnStreamHeaderList(/* fin = */ false, frame_length, headers);
  } else {
    spdy_session_->OnHeaderList(headers);
  }
  qpack_decoded_headers_accumulator_.reset();
}

size_t QuicSpdyStream::WriteHeadersImpl(
    spdy::SpdyHeaderBlock header_block,
    bool fin,
    QuicReferenceCountedPointer<QuicAckListenerInterface> ack_listener) {
  if (!VersionUsesQpack(transport_version())) {
    return spdy_session_->WriteHeadersOnHeadersStream(
        id(), std::move(header_block), fin, precedence(),
        std::move(ack_listener));
  }

  if (session()->perspective() == Perspective::IS_CLIENT && !priority_sent_) {
    PriorityFrame frame;
    PopulatePriorityFrame(&frame);
    spdy_session_->WriteH3Priority(frame);
    priority_sent_ = true;
  }

  // Encode header list.
  std::string encoded_headers =
      spdy_session_->qpack_encoder()->EncodeHeaderList(id(), header_block);

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

void QuicSpdyStream::PopulatePriorityFrame(PriorityFrame* frame) {
  frame->weight = precedence().spdy3_priority();
  frame->dependency_type = ROOT_OF_TREE;
  frame->prioritized_type = REQUEST_STREAM;
  frame->prioritized_element_id = id();
}

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
