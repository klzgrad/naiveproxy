// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/http/quic_spdy_stream.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "absl/base/macros.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "quiche/http2/http2_constants.h"
#include "quiche/quic/core/http/http_constants.h"
#include "quiche/quic/core/http/http_decoder.h"
#include "quiche/quic/core/http/http_frames.h"
#include "quiche/quic/core/http/quic_spdy_session.h"
#include "quiche/quic/core/http/spdy_utils.h"
#include "quiche/quic/core/http/web_transport_http3.h"
#include "quiche/quic/core/qpack/qpack_decoder.h"
#include "quiche/quic/core/qpack/qpack_encoder.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/quic/core/quic_versions.h"
#include "quiche/quic/core/quic_write_blocked_list.h"
#include "quiche/quic/core/web_transport_interface.h"
#include "quiche/quic/platform/api/quic_bug_tracker.h"
#include "quiche/quic/platform/api/quic_flag_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/quic/platform/api/quic_logging.h"
#include "quiche/common/capsule.h"
#include "quiche/common/quiche_mem_slice_storage.h"
#include "quiche/common/quiche_text_utils.h"
#include "quiche/spdy/core/spdy_protocol.h"

using ::quiche::Capsule;
using ::quiche::CapsuleType;
using ::spdy::Http2HeaderBlock;

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

  bool OnMaxPushIdFrame() override {
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

  bool OnDataFramePayload(absl::string_view payload) override {
    QUICHE_DCHECK(!payload.empty());
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

  bool OnHeadersFramePayload(absl::string_view payload) override {
    QUICHE_DCHECK(!payload.empty());
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

  bool OnPriorityUpdateFrameStart(QuicByteCount /*header_length*/) override {
    CloseConnectionOnWrongFrame("Priority update");
    return false;
  }

  bool OnPriorityUpdateFrame(const PriorityUpdateFrame& /*frame*/) override {
    CloseConnectionOnWrongFrame("Priority update");
    return false;
  }

  bool OnAcceptChFrameStart(QuicByteCount /*header_length*/) override {
    CloseConnectionOnWrongFrame("ACCEPT_CH");
    return false;
  }

  bool OnAcceptChFrame(const AcceptChFrame& /*frame*/) override {
    CloseConnectionOnWrongFrame("ACCEPT_CH");
    return false;
  }

  void OnWebTransportStreamFrameType(
      QuicByteCount header_length, WebTransportSessionId session_id) override {
    stream_->OnWebTransportStreamFrameType(header_length, session_id);
  }

  bool OnUnknownFrameStart(uint64_t frame_type, QuicByteCount header_length,
                           QuicByteCount payload_length) override {
    return stream_->OnUnknownFrameStart(frame_type, header_length,
                                        payload_length);
  }

  bool OnUnknownFramePayload(absl::string_view payload) override {
    return stream_->OnUnknownFramePayload(payload);
  }

  bool OnUnknownFrameEnd() override { return stream_->OnUnknownFrameEnd(); }

 private:
  void CloseConnectionOnWrongFrame(absl::string_view frame_type) {
    stream_->OnUnrecoverableError(
        QUIC_HTTP_FRAME_UNEXPECTED_ON_SPDY_STREAM,
        absl::StrCat(frame_type, " frame received on data stream"));
  }

  QuicSpdyStream* stream_;
};

#define ENDPOINT                                                   \
  (session()->perspective() == Perspective::IS_SERVER ? "Server: " \
                                                      : "Client:"  \
                                                        " ")

namespace {
HttpDecoder::Options HttpDecoderOptionsForBidiStream(
    QuicSpdySession* spdy_session) {
  HttpDecoder::Options options;
  options.allow_web_transport_stream =
      spdy_session->WillNegotiateWebTransport();
  return options;
}
}  // namespace

QuicSpdyStream::QuicSpdyStream(QuicStreamId id, QuicSpdySession* spdy_session,
                               StreamType type)
    : QuicStream(id, spdy_session, /*is_static=*/false, type),
      spdy_session_(spdy_session),
      on_body_available_called_because_sequencer_is_closed_(false),
      visitor_(nullptr),
      blocked_on_decoding_headers_(false),
      headers_decompressed_(false),
      header_list_size_limit_exceeded_(false),
      headers_payload_length_(0),
      trailers_decompressed_(false),
      trailers_consumed_(false),
      http_decoder_visitor_(std::make_unique<HttpDecoderVisitor>(this)),
      decoder_(http_decoder_visitor_.get(),
               HttpDecoderOptionsForBidiStream(spdy_session)),
      sequencer_offset_(0),
      is_decoder_processing_input_(false),
      ack_listener_(nullptr) {
  QUICHE_DCHECK_EQ(session()->connection(), spdy_session->connection());
  QUICHE_DCHECK_EQ(transport_version(), spdy_session->transport_version());
  QUICHE_DCHECK(!QuicUtils::IsCryptoStreamId(transport_version(), id));
  QUICHE_DCHECK_EQ(0u, sequencer()->NumBytesConsumed());
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
                               QuicSpdySession* spdy_session)
    : QuicStream(pending, spdy_session, /*is_static=*/false),
      spdy_session_(spdy_session),
      on_body_available_called_because_sequencer_is_closed_(false),
      visitor_(nullptr),
      blocked_on_decoding_headers_(false),
      headers_decompressed_(false),
      header_list_size_limit_exceeded_(false),
      headers_payload_length_(0),
      trailers_decompressed_(false),
      trailers_consumed_(false),
      http_decoder_visitor_(std::make_unique<HttpDecoderVisitor>(this)),
      decoder_(http_decoder_visitor_.get()),
      sequencer_offset_(sequencer()->NumBytesConsumed()),
      is_decoder_processing_input_(false),
      ack_listener_(nullptr) {
  QUICHE_DCHECK_EQ(session()->connection(), spdy_session->connection());
  QUICHE_DCHECK_EQ(transport_version(), spdy_session->transport_version());
  QUICHE_DCHECK(!QuicUtils::IsCryptoStreamId(transport_version(), id()));
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
    Http2HeaderBlock header_block, bool fin,
    quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
        ack_listener) {
  if (!AssertNotWebTransportDataStream("writing headers")) {
    return 0;
  }

  QuicConnection::ScopedPacketFlusher flusher(spdy_session_->connection());

  MaybeProcessSentWebTransportHeaders(header_block);

  if (web_transport_ != nullptr &&
      spdy_session_->perspective() == Perspective::IS_SERVER) {
    header_block["sec-webtransport-http3-draft"] = "draft02";
  }

  size_t bytes_written =
      WriteHeadersImpl(std::move(header_block), fin, std::move(ack_listener));
  if (!VersionUsesHttp3(transport_version()) && fin) {
    // If HEADERS are sent on the headers stream, then |fin_sent_| needs to be
    // set and write side needs to be closed without actually sending a FIN on
    // this stream.
    // TODO(rch): Add test to ensure fin_sent_ is set whenever a fin is sent.
    SetFinSent();
    CloseWriteSide();
  }

  if (web_transport_ != nullptr &&
      session()->perspective() == Perspective::IS_CLIENT) {
    WriteGreaseCapsule();
    if (spdy_session_->http_datagram_support() ==
        HttpDatagramSupport::kDraft04) {
      // Send a REGISTER_DATAGRAM_NO_CONTEXT capsule to support servers that
      // are running draft-ietf-masque-h3-datagram-04 or -05.
      uint64_t capsule_type = 0xff37a2;  // REGISTER_DATAGRAM_NO_CONTEXT
      constexpr unsigned char capsule_data[4] = {
          0x80, 0xff, 0x7c, 0x00,  // WEBTRANSPORT datagram format type
      };
      WriteCapsule(Capsule::Unknown(
          capsule_type,
          absl::string_view(reinterpret_cast<const char*>(capsule_data),
                            sizeof(capsule_data))));
      WriteGreaseCapsule();
    }
  }

  if (connect_ip_visitor_ != nullptr) {
    connect_ip_visitor_->OnHeadersWritten();
  }

  return bytes_written;
}

void QuicSpdyStream::WriteOrBufferBody(absl::string_view data, bool fin) {
  if (!AssertNotWebTransportDataStream("writing body data")) {
    return;
  }
  if (!VersionUsesHttp3(transport_version()) || data.length() == 0) {
    WriteOrBufferData(data, fin, nullptr);
    return;
  }
  QuicConnection::ScopedPacketFlusher flusher(spdy_session_->connection());

  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnDataFrameSent(id(), data.length());
  }

  const bool success =
      WriteDataFrameHeader(data.length(), /*force_write=*/true);
  QUICHE_DCHECK(success);

  // Write body.
  QUIC_DLOG(INFO) << ENDPOINT << "Stream " << id()
                  << " is writing DATA frame payload of length "
                  << data.length() << " with fin " << fin;
  WriteOrBufferData(data, fin, nullptr);
}

size_t QuicSpdyStream::WriteTrailers(
    Http2HeaderBlock trailer_block,
    quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
        ack_listener) {
  if (fin_sent()) {
    QUIC_BUG(quic_bug_10410_1)
        << "Trailers cannot be sent after a FIN, on stream " << id();
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
        std::make_pair(kFinalOffsetHeaderKey, absl::StrCat(final_offset)));
  }

  // Write the trailing headers with a FIN, and close stream for writing:
  // trailers are the last thing to be sent on a stream.
  const bool kFin = true;
  size_t bytes_written =
      WriteHeadersImpl(std::move(trailer_block), kFin, std::move(ack_listener));

  // If trailers are sent on the headers stream, then |fin_sent_| needs to be
  // set without actually sending a FIN on this stream.
  if (!VersionUsesHttp3(transport_version())) {
    SetFinSent();

    // Also, write side of this stream needs to be closed.  However, only do
    // this if there is no more buffered data, otherwise it will never be sent.
    if (BufferedDataBytes() == 0) {
      CloseWriteSide();
    }
  }

  return bytes_written;
}

QuicConsumedData QuicSpdyStream::WritevBody(const struct iovec* iov, int count,
                                            bool fin) {
  quiche::QuicheMemSliceStorage storage(
      iov, count,
      session()->connection()->helper()->GetStreamSendBufferAllocator(),
      GetQuicFlag(quic_send_buffer_max_data_slice_size));
  return WriteBodySlices(storage.ToSpan(), fin);
}

bool QuicSpdyStream::WriteDataFrameHeader(QuicByteCount data_length,
                                          bool force_write) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  QUICHE_DCHECK_GT(data_length, 0u);
  quiche::QuicheBuffer header = HttpEncoder::SerializeDataFrameHeader(
      data_length,
      spdy_session_->connection()->helper()->GetStreamSendBufferAllocator());
  const bool can_write = CanWriteNewDataAfterData(header.size());
  if (!can_write && !force_write) {
    return false;
  }

  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnDataFrameSent(id(), data_length);
  }

  unacked_frame_headers_offsets_.Add(
      send_buffer().stream_offset(),
      send_buffer().stream_offset() + header.size());
  QUIC_DLOG(INFO) << ENDPOINT << "Stream " << id()
                  << " is writing DATA frame header of length "
                  << header.size();
  if (can_write) {
    // Save one copy and allocation if send buffer can accomodate the header.
    quiche::QuicheMemSlice header_slice(std::move(header));
    WriteMemSlices(absl::MakeSpan(&header_slice, 1), false);
  } else {
    QUICHE_DCHECK(force_write);
    WriteOrBufferData(header.AsStringView(), false, nullptr);
  }
  return true;
}

QuicConsumedData QuicSpdyStream::WriteBodySlices(
    absl::Span<quiche::QuicheMemSlice> slices, bool fin) {
  if (!VersionUsesHttp3(transport_version()) || slices.empty()) {
    return WriteMemSlices(slices, fin);
  }

  QuicConnection::ScopedPacketFlusher flusher(spdy_session_->connection());
  const QuicByteCount data_size = MemSliceSpanTotalSize(slices);
  if (!WriteDataFrameHeader(data_size, /*force_write=*/false)) {
    return {0, false};
  }

  QUIC_DLOG(INFO) << ENDPOINT << "Stream " << id()
                  << " is writing DATA frame payload of length " << data_size;
  return WriteMemSlices(slices, fin);
}

size_t QuicSpdyStream::Readv(const struct iovec* iov, size_t iov_len) {
  QUICHE_DCHECK(FinishedReadingHeaders());
  if (!VersionUsesHttp3(transport_version())) {
    return sequencer()->Readv(iov, iov_len);
  }
  size_t bytes_read = 0;
  sequencer()->MarkConsumed(body_manager_.ReadBody(iov, iov_len, &bytes_read));

  return bytes_read;
}

int QuicSpdyStream::GetReadableRegions(iovec* iov, size_t iov_len) const {
  QUICHE_DCHECK(FinishedReadingHeaders());
  if (!VersionUsesHttp3(transport_version())) {
    return sequencer()->GetReadableRegions(iov, iov_len);
  }
  return body_manager_.PeekBody(iov, iov_len);
}

void QuicSpdyStream::MarkConsumed(size_t num_bytes) {
  QUICHE_DCHECK(FinishedReadingHeaders());
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

void QuicSpdyStream::MarkTrailersConsumed() { trailers_consumed_ = true; }

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
    HandleBodyAvailable();
    return;
  }

  if (sequencer()->IsClosed() &&
      !on_body_available_called_because_sequencer_is_closed_) {
    on_body_available_called_because_sequencer_is_closed_ = true;
    HandleBodyAvailable();
  }
}

void QuicSpdyStream::OnStreamHeadersPriority(
    const spdy::SpdyStreamPrecedence& precedence) {
  QUICHE_DCHECK_EQ(Perspective::IS_SERVER,
                   session()->connection()->perspective());
  SetPriority(QuicStreamPriority{precedence.spdy3_priority(),
                                 QuicStreamPriority::kDefaultIncremental});
}

void QuicSpdyStream::OnStreamHeaderList(bool fin, size_t frame_len,
                                        const QuicHeaderList& header_list) {
  if (!spdy_session()->user_agent_id().has_value()) {
    std::string uaid;
    for (const auto& kv : header_list) {
      if (quiche::QuicheTextUtils::ToLower(kv.first) == kUserAgentHeaderName) {
        uaid = kv.second;
        break;
      }
    }
    spdy_session()->SetUserAgentId(std::move(uaid));
  }

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

    OnStreamHeaderList(/* fin = */ false, headers_payload_length_, headers);
  } else {
    spdy_session_->OnHeaderList(headers);
  }

  if (blocked_on_decoding_headers_) {
    blocked_on_decoding_headers_ = false;
    // Continue decoding HTTP/3 frames.
    OnDataAvailable();
  }
}

void QuicSpdyStream::OnHeaderDecodingError(QuicErrorCode error_code,
                                           absl::string_view error_message) {
  qpack_decoded_headers_accumulator_.reset();

  std::string connection_close_error_message = absl::StrCat(
      "Error decoding ", headers_decompressed_ ? "trailers" : "headers",
      " on stream ", id(), ": ", error_message);
  OnUnrecoverableError(error_code, connection_close_error_message);
}

void QuicSpdyStream::MaybeSendPriorityUpdateFrame() {
  if (!VersionUsesHttp3(transport_version()) ||
      session()->perspective() != Perspective::IS_CLIENT) {
    return;
  }

  if (last_sent_priority_ == priority()) {
    return;
  }
  last_sent_priority_ = priority();

  spdy_session_->WriteHttp3PriorityUpdate(id(), priority());
}

void QuicSpdyStream::OnHeadersTooLarge() { Reset(QUIC_HEADERS_TOO_LARGE); }

void QuicSpdyStream::OnInitialHeadersComplete(
    bool fin, size_t /*frame_len*/, const QuicHeaderList& header_list) {
  // TODO(b/134706391): remove |fin| argument.
  headers_decompressed_ = true;
  header_list_ = header_list;
  bool header_too_large = VersionUsesHttp3(transport_version())
                              ? header_list_size_limit_exceeded_
                              : header_list.empty();
  if (!AreHeaderFieldValuesValid(header_list)) {
    OnInvalidHeaders();
    return;
  }
  // Validate request headers if it did not exceed size limit. If it did,
  // OnHeadersTooLarge() should have already handled it previously.
  if (!header_too_large && !AreHeadersValid(header_list)) {
    QUIC_CODE_COUNT_N(quic_validate_request_header, 1, 2);
    if (GetQuicReloadableFlag(quic_act_upon_invalid_header)) {
      QUIC_RELOADABLE_FLAG_COUNT(quic_act_upon_invalid_header);
      OnInvalidHeaders();
      return;
    }
  }
  QUIC_CODE_COUNT_N(quic_validate_request_header, 2, 2);

  if (!GetQuicReloadableFlag(quic_verify_request_headers_2) ||
      !header_too_large) {
    MaybeProcessReceivedWebTransportHeaders();
  }

  if (VersionUsesHttp3(transport_version())) {
    if (fin) {
      OnStreamFrame(QuicStreamFrame(id(), /* fin = */ true,
                                    highest_received_byte_offset(),
                                    absl::string_view()));
    }
    return;
  }

  if (fin && !rst_sent()) {
    OnStreamFrame(
        QuicStreamFrame(id(), fin, /* offset = */ 0, absl::string_view()));
  }
  if (FinishedReadingHeaders()) {
    sequencer()->SetUnblocked();
  }
}

void QuicSpdyStream::OnPromiseHeaderList(
    QuicStreamId /* promised_id */, size_t /* frame_len */,
    const QuicHeaderList& /*header_list */) {
  // To be overridden in QuicSpdyClientStream.  Not supported on
  // server side.
  stream_delegate()->OnStreamError(QUIC_INVALID_HEADERS_STREAM_DATA,
                                   "Promise headers received by server");
}

bool QuicSpdyStream::CopyAndValidateTrailers(const QuicHeaderList& header_list,
                                             bool expect_final_byte_offset,
                                             size_t* final_byte_offset,
                                             spdy::Http2HeaderBlock* trailers) {
  return SpdyUtils::CopyAndValidateTrailers(
      header_list, expect_final_byte_offset, final_byte_offset, trailers);
}

void QuicSpdyStream::OnTrailingHeadersComplete(
    bool fin, size_t /*frame_len*/, const QuicHeaderList& header_list) {
  // TODO(b/134706391): remove |fin| argument.
  QUICHE_DCHECK(!trailers_decompressed_);
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
  if (!CopyAndValidateTrailers(header_list, expect_final_byte_offset,
                               &final_byte_offset, &received_trailers_)) {
    QUIC_DLOG(ERROR) << ENDPOINT << "Trailers for stream " << id()
                     << " are malformed.";
    stream_delegate()->OnStreamError(QUIC_INVALID_HEADERS_STREAM_DATA,
                                     "Trailers are malformed");
    return;
  }
  trailers_decompressed_ = true;
  if (fin) {
    const QuicStreamOffset offset = VersionUsesHttp3(transport_version())
                                        ? highest_received_byte_offset()
                                        : final_byte_offset;
    OnStreamFrame(QuicStreamFrame(id(), fin, offset, absl::string_view()));
  }
}

void QuicSpdyStream::OnPriorityFrame(
    const spdy::SpdyStreamPrecedence& precedence) {
  QUICHE_DCHECK_EQ(Perspective::IS_SERVER,
                   session()->connection()->perspective());
  SetPriority(QuicStreamPriority{precedence.spdy3_priority(),
                                 QuicStreamPriority::kDefaultIncremental});
}

void QuicSpdyStream::OnStreamReset(const QuicRstStreamFrame& frame) {
  if (web_transport_data_ != nullptr) {
    WebTransportStreamVisitor* webtransport_visitor =
        web_transport_data_->adapter.visitor();
    if (webtransport_visitor != nullptr) {
      webtransport_visitor->OnResetStreamReceived(
          Http3ErrorToWebTransportOrDefault(frame.ietf_error_code));
    }
    QuicStream::OnStreamReset(frame);
    return;
  }

  if (VersionUsesHttp3(transport_version()) && !fin_received() &&
      spdy_session_->qpack_decoder()) {
    spdy_session_->qpack_decoder()->OnStreamReset(id());
    qpack_decoded_headers_accumulator_.reset();
  }

  if (VersionUsesHttp3(transport_version()) ||
      frame.error_code != QUIC_STREAM_NO_ERROR) {
    QuicStream::OnStreamReset(frame);
    return;
  }

  QUIC_DVLOG(1) << ENDPOINT
                << "Received QUIC_STREAM_NO_ERROR, not discarding response";
  set_rst_received(true);
  MaybeIncreaseHighestReceivedOffset(frame.byte_offset);
  set_stream_error(frame.error());
  CloseWriteSide();
}

void QuicSpdyStream::ResetWithError(QuicResetStreamError error) {
  if (VersionUsesHttp3(transport_version()) && !fin_received() &&
      spdy_session_->qpack_decoder() && web_transport_data_ == nullptr) {
    spdy_session_->qpack_decoder()->OnStreamReset(id());
    qpack_decoded_headers_accumulator_.reset();
  }

  QuicStream::ResetWithError(error);
}

bool QuicSpdyStream::OnStopSending(QuicResetStreamError error) {
  if (web_transport_data_ != nullptr) {
    WebTransportStreamVisitor* visitor = web_transport_data_->adapter.visitor();
    if (visitor != nullptr) {
      visitor->OnStopSendingReceived(
          Http3ErrorToWebTransportOrDefault(error.ietf_application_code()));
    }
  }

  return QuicStream::OnStopSending(error);
}

void QuicSpdyStream::OnWriteSideInDataRecvdState() {
  if (web_transport_data_ != nullptr) {
    WebTransportStreamVisitor* visitor = web_transport_data_->adapter.visitor();
    if (visitor != nullptr) {
      visitor->OnWriteSideInDataRecvdState();
    }
  }

  QuicStream::OnWriteSideInDataRecvdState();
}

void QuicSpdyStream::OnDataAvailable() {
  if (!VersionUsesHttp3(transport_version())) {
    // Sequencer must be blocked until headers are consumed.
    QUICHE_DCHECK(FinishedReadingHeaders());
  }

  if (!VersionUsesHttp3(transport_version())) {
    HandleBodyAvailable();
    return;
  }

  if (web_transport_data_ != nullptr) {
    web_transport_data_->adapter.OnDataAvailable();
    return;
  }

  if (!spdy_session()->ShouldProcessIncomingRequests()) {
    spdy_session()->OnStreamWaitingForClientSettings(id());
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
    QUICHE_DCHECK_GE(sequencer_offset_, sequencer()->NumBytesConsumed());
    if (!sequencer()->PeekRegion(sequencer_offset_, &iov)) {
      break;
    }

    QUICHE_DCHECK(!sequencer()->IsClosed());
    is_decoder_processing_input_ = true;
    QuicByteCount processed_bytes = decoder_.ProcessInput(
        reinterpret_cast<const char*>(iov.iov_base), iov.iov_len);
    is_decoder_processing_input_ = false;
    if (!session()->connection()->connected()) {
      return;
    }
    sequencer_offset_ += processed_bytes;
    if (blocked_on_decoding_headers_) {
      return;
    }
    if (web_transport_data_ != nullptr) {
      return;
    }
  }

  // Do not call HandleBodyAvailable() until headers are consumed.
  if (!FinishedReadingHeaders()) {
    return;
  }

  if (body_manager_.HasBytesToRead()) {
    HandleBodyAvailable();
    return;
  }

  if (sequencer()->IsClosed() &&
      !on_body_available_called_because_sequencer_is_closed_) {
    on_body_available_called_because_sequencer_is_closed_ = true;
    HandleBodyAvailable();
  }
}

void QuicSpdyStream::OnClose() {
  QuicStream::OnClose();

  qpack_decoded_headers_accumulator_.reset();

  if (visitor_) {
    Visitor* visitor = visitor_;
    // Calling Visitor::OnClose() may result the destruction of the visitor,
    // so we need to ensure we don't call it again.
    visitor_ = nullptr;
    visitor->OnClose(this);
  }

  if (web_transport_ != nullptr) {
    web_transport_->OnConnectStreamClosing();
  }
  if (web_transport_data_ != nullptr) {
    WebTransportHttp3* web_transport =
        spdy_session_->GetWebTransportSession(web_transport_data_->session_id);
    if (web_transport == nullptr) {
      // Since there is no guaranteed destruction order for streams, the session
      // could be already removed from the stream map by the time we reach here.
      QUIC_DLOG(WARNING) << ENDPOINT << "WebTransport stream " << id()
                         << " attempted to notify parent session "
                         << web_transport_data_->session_id
                         << ", but the session could not be found.";
      return;
    }
    web_transport->OnStreamClosed(id());
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

bool QuicSpdyStream::ParseHeaderStatusCode(const Http2HeaderBlock& header,
                                           int* status_code) {
  Http2HeaderBlock::const_iterator it = header.find(spdy::kHttp2StatusHeader);
  if (it == header.end()) {
    return false;
  }
  const absl::string_view status(it->second);
  return ParseHeaderStatusCode(status, status_code);
}

bool QuicSpdyStream::ParseHeaderStatusCode(absl::string_view status,
                                           int* status_code) {
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
  return absl::SimpleAtoi(status, status_code);
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

bool QuicSpdyStream::OnDataFrameStart(QuicByteCount header_length,
                                      QuicByteCount payload_length) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));

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

bool QuicSpdyStream::OnDataFramePayload(absl::string_view payload) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));

  body_manager_.OnBody(payload);

  return true;
}

bool QuicSpdyStream::OnDataFrameEnd() {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));

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
  QUICHE_DCHECK_LE(newly_acked_header_length, *newly_acked_length);
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
  QUICHE_DCHECK_LE(retransmitted_header_length, data_length);

  if (ack_listener_ != nullptr) {
    ack_listener_->OnPacketRetransmitted(data_length -
                                         retransmitted_header_length);
  }
}

QuicByteCount QuicSpdyStream::GetNumFrameHeadersInInterval(
    QuicStreamOffset offset, QuicByteCount data_length) const {
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
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));
  QUICHE_DCHECK(!qpack_decoded_headers_accumulator_);

  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnHeadersFrameReceived(id(),
                                                           payload_length);
  }

  headers_payload_length_ = payload_length;

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

bool QuicSpdyStream::OnHeadersFramePayload(absl::string_view payload) {
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));

  if (!qpack_decoded_headers_accumulator_) {
    QUIC_BUG(b215142466_OnHeadersFramePayload);
    OnHeaderDecodingError(QUIC_INTERNAL_ERROR,
                          "qpack_decoded_headers_accumulator_ is nullptr");
    return false;
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
  QUICHE_DCHECK(VersionUsesHttp3(transport_version()));

  if (!qpack_decoded_headers_accumulator_) {
    QUIC_BUG(b215142466_OnHeadersFrameEnd);
    OnHeaderDecodingError(QUIC_INTERNAL_ERROR,
                          "qpack_decoded_headers_accumulator_ is nullptr");
    return false;
  }

  qpack_decoded_headers_accumulator_->EndHeaderBlock();

  // If decoding is complete or an error is detected, then
  // |qpack_decoded_headers_accumulator_| is already reset.
  if (qpack_decoded_headers_accumulator_) {
    blocked_on_decoding_headers_ = true;
    return false;
  }

  return !sequencer()->IsClosed() && !reading_stopped();
}

void QuicSpdyStream::OnWebTransportStreamFrameType(
    QuicByteCount header_length, WebTransportSessionId session_id) {
  QUIC_DVLOG(1) << ENDPOINT << " Received WEBTRANSPORT_STREAM on stream "
                << id() << " for session " << session_id;
  sequencer()->MarkConsumed(header_length);

  if (headers_payload_length_ > 0 || headers_decompressed_) {
    QUIC_PEER_BUG(WEBTRANSPORT_STREAM received on HTTP request)
        << ENDPOINT << "Stream " << id()
        << " tried to convert to WebTransport, but it already "
           "has HTTP data on it";
    Reset(QUIC_STREAM_FRAME_UNEXPECTED);
  }
  if (QuicUtils::IsOutgoingStreamId(spdy_session_->version(), id(),
                                    spdy_session_->perspective())) {
    QUIC_PEER_BUG(WEBTRANSPORT_STREAM received on outgoing request)
        << ENDPOINT << "Stream " << id()
        << " tried to convert to WebTransport, but only the "
           "initiator of the stream can do it.";
    Reset(QUIC_STREAM_FRAME_UNEXPECTED);
  }

  QUICHE_DCHECK(web_transport_ == nullptr);
  web_transport_data_ =
      std::make_unique<WebTransportDataStream>(this, session_id);
  spdy_session_->AssociateIncomingWebTransportStreamWithSession(session_id,
                                                                id());
}

bool QuicSpdyStream::OnUnknownFrameStart(uint64_t frame_type,
                                         QuicByteCount header_length,
                                         QuicByteCount payload_length) {
  if (spdy_session_->debug_visitor()) {
    spdy_session_->debug_visitor()->OnUnknownFrameReceived(id(), frame_type,
                                                           payload_length);
  }
  spdy_session_->OnUnknownFrameStart(id(), frame_type, header_length,
                                     payload_length);

  // Consume the frame header.
  QUIC_DVLOG(1) << ENDPOINT << "Consuming " << header_length
                << " byte long frame header of frame of unknown type "
                << frame_type << ".";
  sequencer()->MarkConsumed(body_manager_.OnNonBody(header_length));
  return true;
}

bool QuicSpdyStream::OnUnknownFramePayload(absl::string_view payload) {
  spdy_session_->OnUnknownFramePayload(id(), payload);

  // Consume the frame payload.
  QUIC_DVLOG(1) << ENDPOINT << "Consuming " << payload.size()
                << " bytes of payload of frame of unknown type.";
  sequencer()->MarkConsumed(body_manager_.OnNonBody(payload.size()));
  return true;
}

bool QuicSpdyStream::OnUnknownFrameEnd() { return true; }

size_t QuicSpdyStream::WriteHeadersImpl(
    spdy::Http2HeaderBlock header_block, bool fin,
    quiche::QuicheReferenceCountedPointer<QuicAckListenerInterface>
        ack_listener) {
  if (!VersionUsesHttp3(transport_version())) {
    return spdy_session_->WriteHeadersOnHeadersStream(
        id(), std::move(header_block), fin,
        spdy::SpdyStreamPrecedence(priority().urgency),
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
  std::string headers_frame_header =
      HttpEncoder::SerializeHeadersFrameHeader(encoded_headers.size());
  unacked_frame_headers_offsets_.Add(
      send_buffer().stream_offset(),
      send_buffer().stream_offset() + headers_frame_header.length());

  QUIC_DLOG(INFO) << ENDPOINT << "Stream " << id()
                  << " is writing HEADERS frame header of length "
                  << headers_frame_header.length() << ", and payload of length "
                  << encoded_headers.length() << " with fin " << fin;
  WriteOrBufferData(absl::StrCat(headers_frame_header, encoded_headers), fin,
                    /*ack_listener=*/nullptr);

  QuicSpdySession::LogHeaderCompressionRatioHistogram(
      /* using_qpack = */ true,
      /* is_sent = */ true,
      encoded_headers.size() + encoder_stream_sent_byte_count,
      header_block.TotalBytesUsed());

  return encoded_headers.size();
}

bool QuicSpdyStream::CanWriteNewBodyData(QuicByteCount write_size) const {
  QUICHE_DCHECK_NE(0u, write_size);
  if (!VersionUsesHttp3(transport_version())) {
    return CanWriteNewData();
  }

  return CanWriteNewDataAfterData(
      HttpEncoder::GetDataFrameHeaderLength(write_size));
}

void QuicSpdyStream::MaybeProcessReceivedWebTransportHeaders() {
  if (!spdy_session_->SupportsWebTransport()) {
    return;
  }
  if (session()->perspective() != Perspective::IS_SERVER) {
    return;
  }
  QUICHE_DCHECK(IsValidWebTransportSessionId(id(), version()));

  std::string method;
  std::string protocol;
  for (const auto& [header_name, header_value] : header_list_) {
    if (header_name == ":method") {
      if (!method.empty() || header_value.empty()) {
        return;
      }
      method = header_value;
    }
    if (header_name == ":protocol") {
      if (!protocol.empty() || header_value.empty()) {
        return;
      }
      protocol = header_value;
    }
    if (header_name == "datagram-flow-id") {
      QUIC_DLOG(ERROR) << ENDPOINT
                       << "Rejecting WebTransport due to unexpected "
                          "Datagram-Flow-Id header";
      return;
    }
    if (header_name == "sec-webtransport-http3-draft02") {
      if (header_value != "1") {
        QUIC_DLOG(ERROR) << ENDPOINT
                         << "Rejecting WebTransport due to invalid value of "
                            "Sec-Webtransport-Http3-Draft02 header";
        return;
      }
    }
  }

  if (method != "CONNECT" || protocol != "webtransport") {
    return;
  }

  web_transport_ =
      std::make_unique<WebTransportHttp3>(spdy_session_, this, id());
}

void QuicSpdyStream::MaybeProcessSentWebTransportHeaders(
    spdy::Http2HeaderBlock& headers) {
  if (!spdy_session_->SupportsWebTransport()) {
    return;
  }
  if (session()->perspective() != Perspective::IS_CLIENT) {
    return;
  }
  QUICHE_DCHECK(IsValidWebTransportSessionId(id(), version()));

  const auto method_it = headers.find(":method");
  const auto protocol_it = headers.find(":protocol");
  if (method_it == headers.end() || protocol_it == headers.end()) {
    return;
  }
  if (method_it->second != "CONNECT" && protocol_it->second != "webtransport") {
    return;
  }

  headers["sec-webtransport-http3-draft02"] = "1";

  web_transport_ =
      std::make_unique<WebTransportHttp3>(spdy_session_, this, id());
}

void QuicSpdyStream::OnCanWriteNewData() {
  if (web_transport_data_ != nullptr) {
    web_transport_data_->adapter.OnCanWriteNewData();
  }
}

bool QuicSpdyStream::AssertNotWebTransportDataStream(
    absl::string_view operation) {
  if (web_transport_data_ != nullptr) {
    QUIC_BUG(Invalid operation on WebTransport stream)
        << "Attempted to " << operation << " on WebTransport data stream "
        << id() << " associated with session "
        << web_transport_data_->session_id;
    OnUnrecoverableError(QUIC_INTERNAL_ERROR,
                         absl::StrCat("Attempted to ", operation,
                                      " on WebTransport data stream"));
    return false;
  }
  return true;
}

void QuicSpdyStream::ConvertToWebTransportDataStream(
    WebTransportSessionId session_id) {
  if (send_buffer().stream_offset() != 0) {
    QUIC_BUG(Sending WEBTRANSPORT_STREAM when data already sent)
        << "Attempted to send a WEBTRANSPORT_STREAM frame when other data has "
           "already been sent on the stream.";
    OnUnrecoverableError(QUIC_INTERNAL_ERROR,
                         "Attempted to send a WEBTRANSPORT_STREAM frame when "
                         "other data has already been sent on the stream.");
    return;
  }

  std::string header =
      HttpEncoder::SerializeWebTransportStreamFrameHeader(session_id);
  if (header.empty()) {
    QUIC_BUG(Failed to serialize WEBTRANSPORT_STREAM)
        << "Failed to serialize a WEBTRANSPORT_STREAM frame.";
    OnUnrecoverableError(QUIC_INTERNAL_ERROR,
                         "Failed to serialize a WEBTRANSPORT_STREAM frame.");
    return;
  }

  WriteOrBufferData(header, /*fin=*/false, nullptr);
  web_transport_data_ =
      std::make_unique<WebTransportDataStream>(this, session_id);
  QUIC_DVLOG(1) << ENDPOINT << "Successfully opened WebTransport data stream "
                << id() << " for session " << session_id;
}

QuicSpdyStream::WebTransportDataStream::WebTransportDataStream(
    QuicSpdyStream* stream, WebTransportSessionId session_id)
    : session_id(session_id),
      adapter(stream->spdy_session_, stream, stream->sequencer()) {}

void QuicSpdyStream::HandleReceivedDatagram(absl::string_view payload) {
  if (datagram_visitor_ == nullptr) {
    QUIC_DLOG(ERROR) << ENDPOINT << "Received datagram without any visitor";
    return;
  }
  datagram_visitor_->OnHttp3Datagram(id(), payload);
}

bool QuicSpdyStream::OnCapsule(const Capsule& capsule) {
  QUIC_DLOG(INFO) << ENDPOINT << "Stream " << id() << " received capsule "
                  << capsule;
  if (!headers_decompressed_) {
    QUIC_PEER_BUG(capsule before headers)
        << ENDPOINT << "Stream " << id() << " received capsule " << capsule
        << " before headers";
    return false;
  }
  if (web_transport_ != nullptr && web_transport_->close_received()) {
    QUIC_PEER_BUG(capsule after close)
        << ENDPOINT << "Stream " << id() << " received capsule " << capsule
        << " after CLOSE_WEBTRANSPORT_SESSION.";
    return false;
  }
  switch (capsule.capsule_type()) {
    case CapsuleType::DATAGRAM: {
      HandleReceivedDatagram(capsule.datagram_capsule().http_datagram_payload);
    } break;
    case CapsuleType::LEGACY_DATAGRAM: {
      HandleReceivedDatagram(
          capsule.legacy_datagram_capsule().http_datagram_payload);
    } break;
    case CapsuleType::LEGACY_DATAGRAM_WITHOUT_CONTEXT: {
      HandleReceivedDatagram(capsule.legacy_datagram_without_context_capsule()
                                 .http_datagram_payload);
    } break;
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION: {
      if (web_transport_ == nullptr) {
        QUIC_DLOG(ERROR) << ENDPOINT << "Received capsule " << capsule
                         << " for a non-WebTransport stream.";
        return false;
      }
      web_transport_->OnCloseReceived(
          capsule.close_web_transport_session_capsule().error_code,
          capsule.close_web_transport_session_capsule().error_message);
    } break;
    case CapsuleType::ADDRESS_ASSIGN:
      if (connect_ip_visitor_ == nullptr) {
        return true;
      }
      return connect_ip_visitor_->OnAddressAssignCapsule(
          capsule.address_assign_capsule());
    case CapsuleType::ADDRESS_REQUEST:
      if (connect_ip_visitor_ == nullptr) {
        return true;
      }
      return connect_ip_visitor_->OnAddressRequestCapsule(
          capsule.address_request_capsule());
    case CapsuleType::ROUTE_ADVERTISEMENT:
      if (connect_ip_visitor_ == nullptr) {
        return true;
      }
      return connect_ip_visitor_->OnRouteAdvertisementCapsule(
          capsule.route_advertisement_capsule());

    // Ignore WebTransport over HTTP/2 capsules.
    case CapsuleType::WT_RESET_STREAM:
    case CapsuleType::WT_STOP_SENDING:
    case CapsuleType::WT_STREAM:
    case CapsuleType::WT_STREAM_WITH_FIN:
    case CapsuleType::WT_MAX_STREAM_DATA:
    case CapsuleType::WT_MAX_STREAMS_BIDI:
    case CapsuleType::WT_MAX_STREAMS_UNIDI:
      return true;
  }
  return true;
}

void QuicSpdyStream::OnCapsuleParseFailure(absl::string_view error_message) {
  QUIC_DLOG(ERROR) << ENDPOINT << "Capsule parse failure: " << error_message;
  Reset(QUIC_BAD_APPLICATION_PAYLOAD);
}

void QuicSpdyStream::WriteCapsule(const Capsule& capsule, bool fin) {
  QUIC_DLOG(INFO) << ENDPOINT << "Stream " << id() << " sending capsule "
                  << capsule;
  quiche::QuicheBuffer serialized_capsule = SerializeCapsule(
      capsule,
      spdy_session_->connection()->helper()->GetStreamSendBufferAllocator());
  QUICHE_DCHECK_GT(serialized_capsule.size(), 0u);
  WriteOrBufferBody(serialized_capsule.AsStringView(), /*fin=*/fin);
}

void QuicSpdyStream::WriteGreaseCapsule() {
  // GREASE capsulde IDs have a form of 41 * N + 23.
  QuicRandom* random = spdy_session_->connection()->random_generator();
  uint64_t type = random->InsecureRandUint64() >> 4;
  type = (type / 41) * 41 + 23;
  QUICHE_DCHECK_EQ((type - 23) % 41, 0u);

  constexpr size_t kMaxLength = 64;
  size_t length = random->InsecureRandUint64() % kMaxLength;
  std::string bytes(length, '\0');
  random->InsecureRandBytes(&bytes[0], bytes.size());
  Capsule capsule = Capsule::Unknown(type, bytes);
  WriteCapsule(capsule, /*fin=*/false);
}

MessageStatus QuicSpdyStream::SendHttp3Datagram(absl::string_view payload) {
  return spdy_session_->SendHttp3Datagram(id(), payload);
}

void QuicSpdyStream::RegisterHttp3DatagramVisitor(
    Http3DatagramVisitor* visitor) {
  if (visitor == nullptr) {
    QUIC_BUG(null datagram visitor)
        << ENDPOINT << "Null datagram visitor for stream ID " << id();
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Registering datagram visitor with stream ID "
                  << id();

  if (datagram_visitor_ != nullptr) {
    QUIC_BUG(h3 datagram double registration)
        << ENDPOINT
        << "Attempted to doubly register HTTP/3 datagram with stream ID "
        << id();
    return;
  }
  datagram_visitor_ = visitor;
  QUICHE_DCHECK(!capsule_parser_);
  capsule_parser_ = std::make_unique<quiche::CapsuleParser>(this);
}

void QuicSpdyStream::UnregisterHttp3DatagramVisitor() {
  if (datagram_visitor_ == nullptr) {
    QUIC_BUG(datagram visitor empty during unregistration)
        << ENDPOINT << "Cannot unregister datagram visitor for stream ID "
        << id();
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT << "Unregistering datagram visitor for stream ID "
                  << id();
  datagram_visitor_ = nullptr;
}

void QuicSpdyStream::ReplaceHttp3DatagramVisitor(
    Http3DatagramVisitor* visitor) {
  QUIC_BUG_IF(h3 datagram unknown move, datagram_visitor_ == nullptr)
      << "Attempted to move missing datagram visitor on HTTP/3 stream ID "
      << id();
  datagram_visitor_ = visitor;
}

void QuicSpdyStream::RegisterConnectIpVisitor(ConnectIpVisitor* visitor) {
  if (visitor == nullptr) {
    QUIC_BUG(null connect - ip visitor)
        << ENDPOINT << "Null connect-ip visitor for stream ID " << id();
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT
                  << "Registering CONNECT-IP visitor with stream ID " << id();

  if (connect_ip_visitor_ != nullptr) {
    QUIC_BUG(connect - ip double registration)
        << ENDPOINT << "Attempted to doubly register CONNECT-IP with stream ID "
        << id();
    return;
  }
  connect_ip_visitor_ = visitor;
}

void QuicSpdyStream::UnregisterConnectIpVisitor() {
  if (connect_ip_visitor_ == nullptr) {
    QUIC_BUG(connect - ip visitor empty during unregistration)
        << ENDPOINT << "Cannot unregister CONNECT-IP visitor for stream ID "
        << id();
    return;
  }
  QUIC_DLOG(INFO) << ENDPOINT
                  << "Unregistering CONNECT-IP visitor for stream ID " << id();
  connect_ip_visitor_ = nullptr;
}

void QuicSpdyStream::ReplaceConnectIpVisitor(ConnectIpVisitor* visitor) {
  QUIC_BUG_IF(connect - ip unknown move, connect_ip_visitor_ == nullptr)
      << "Attempted to move missing CONNECT-IP visitor on HTTP/3 stream ID "
      << id();
  connect_ip_visitor_ = visitor;
}

void QuicSpdyStream::SetMaxDatagramTimeInQueue(
    QuicTime::Delta max_time_in_queue) {
  spdy_session_->SetMaxDatagramTimeInQueueForStreamId(id(), max_time_in_queue);
}

void QuicSpdyStream::OnDatagramReceived(QuicDataReader* reader) {
  if (!headers_decompressed_) {
    QUIC_DLOG(INFO) << "Dropping datagram received before headers on stream ID "
                    << id();
    return;
  }
  HandleReceivedDatagram(reader->ReadRemainingPayload());
}

QuicByteCount QuicSpdyStream::GetMaxDatagramSize() const {
  QuicByteCount prefix_size = 0;
  switch (spdy_session_->http_datagram_support()) {
    case HttpDatagramSupport::kDraft04:
    case HttpDatagramSupport::kRfc:
      prefix_size =
          QuicDataWriter::GetVarInt62Len(id() / kHttpDatagramStreamIdDivisor);
      break;
    case HttpDatagramSupport::kNone:
    case HttpDatagramSupport::kRfcAndDraft04:
      QUIC_BUG(GetMaxDatagramSize called with no datagram support)
          << "GetMaxDatagramSize() called when no HTTP/3 datagram support has "
             "been negotiated.  Support value: "
          << spdy_session_->http_datagram_support();
      break;
  }
  // If the logic above fails, use the largest possible value as the safe one.
  if (prefix_size == 0) {
    prefix_size = 8;
  }

  QuicByteCount max_datagram_size =
      session()->GetGuaranteedLargestMessagePayload();
  if (max_datagram_size < prefix_size) {
    QUIC_BUG(max_datagram_size smaller than prefix_size)
        << "GetGuaranteedLargestMessagePayload() returned a datagram size that "
           "is not sufficient to fit stream ID into it.";
    return 0;
  }
  return max_datagram_size - prefix_size;
}

void QuicSpdyStream::HandleBodyAvailable() {
  if (!capsule_parser_) {
    OnBodyAvailable();
    return;
  }
  while (body_manager_.HasBytesToRead()) {
    iovec iov;
    int num_iov = GetReadableRegions(&iov, /*iov_len=*/1);
    if (num_iov == 0) {
      break;
    }
    if (!capsule_parser_->IngestCapsuleFragment(absl::string_view(
            reinterpret_cast<const char*>(iov.iov_base), iov.iov_len))) {
      break;
    }
    MarkConsumed(iov.iov_len);
  }
  // If we received a FIN, make sure that there isn't a partial capsule buffered
  // in the capsule parser.
  if (sequencer()->IsClosed()) {
    capsule_parser_->ErrorIfThereIsRemainingBufferedData();
    if (web_transport_ != nullptr) {
      web_transport_->OnConnectStreamFinReceived();
    }
    OnFinRead();
  }
}

namespace {
// Return true if |c| is not allowed in an HTTP/3 wire-encoded header and
// pseudo-header names according to
// https://datatracker.ietf.org/doc/html/draft-ietf-quic-http#section-4.1.1 and
// https://datatracker.ietf.org/doc/html/draft-ietf-httpbis-semantics-19#section-5.6.2
constexpr bool isInvalidHeaderNameCharacter(unsigned char c) {
  if (c == '!' || c == '|' || c == '~' || c == '*' || c == '+' || c == '-' ||
      c == '.' ||
      // #, $, %, &, '
      (c >= '#' && c <= '\'') ||
      // [0-9], :
      (c >= '0' && c <= ':') ||
      // ^, _, `, [a-z]
      (c >= '^' && c <= 'z')) {
    return false;
  }
  return true;
}
}  // namespace

bool QuicSpdyStream::AreHeadersValid(const QuicHeaderList& header_list) const {
  QUICHE_DCHECK(GetQuicReloadableFlag(quic_verify_request_headers_2));
  for (const std::pair<std::string, std::string>& pair : header_list) {
    const std::string& name = pair.first;
    if (std::any_of(name.begin(), name.end(), isInvalidHeaderNameCharacter)) {
      QUIC_DLOG(ERROR) << "Invalid request header " << name;
      return false;
    }
    if (http2::GetInvalidHttp2HeaderSet().contains(name)) {
      QUIC_DLOG(ERROR) << name << " header is not allowed";
      return false;
    }
  }
  return true;
}

bool QuicSpdyStream::AreHeaderFieldValuesValid(
    const QuicHeaderList& header_list) const {
  if (!VersionUsesHttp3(transport_version())) {
    return true;
  }
  // According to https://www.rfc-editor.org/rfc/rfc9114.html#section-10.3
  // "[...] HTTP/3 can transport field values that are not valid. While most
  // values that can be encoded will not alter field parsing, carriage return
  // (ASCII 0x0d), line feed (ASCII 0x0a), and the null character (ASCII 0x00)
  // might be exploited by an attacker if they are translated verbatim. Any
  // request or response that contains a character not permitted in a field
  // value MUST be treated as malformed.
  // [...]"
  for (const std::pair<std::string, std::string>& pair : header_list) {
    const std::string& value = pair.second;
    for (const auto c : value) {
      if (c == '\0' || c == '\n' || c == '\r') {
        return false;
      }
    }
  }
  return true;
}

void QuicSpdyStream::OnInvalidHeaders() { Reset(QUIC_BAD_APPLICATION_PAYLOAD); }

#undef ENDPOINT  // undef for jumbo builds
}  // namespace quic
