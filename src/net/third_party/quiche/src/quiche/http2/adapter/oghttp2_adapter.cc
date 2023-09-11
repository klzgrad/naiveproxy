#include "quiche/http2/adapter/oghttp2_adapter.h"

#include <memory>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "quiche/http2/adapter/http2_util.h"
#include "quiche/common/platform/api/quiche_bug_tracker.h"
#include "quiche/spdy/core/spdy_protocol.h"

namespace http2 {
namespace adapter {

namespace {

using spdy::SpdyGoAwayIR;
using spdy::SpdyPingIR;
using spdy::SpdyPriorityIR;
using spdy::SpdyWindowUpdateIR;

}  // namespace

/* static */
std::unique_ptr<OgHttp2Adapter> OgHttp2Adapter::Create(
    Http2VisitorInterface& visitor, Options options) {
  // Using `new` to access a non-public constructor.
  return absl::WrapUnique(new OgHttp2Adapter(visitor, std::move(options)));
}

OgHttp2Adapter::~OgHttp2Adapter() {}

bool OgHttp2Adapter::IsServerSession() const {
  return session_->IsServerSession();
}

int64_t OgHttp2Adapter::ProcessBytes(absl::string_view bytes) {
  return session_->ProcessBytes(bytes);
}

void OgHttp2Adapter::SubmitSettings(absl::Span<const Http2Setting> settings) {
  session_->SubmitSettings(settings);
}

void OgHttp2Adapter::SubmitPriorityForStream(Http2StreamId stream_id,
                                             Http2StreamId parent_stream_id,
                                             int weight, bool exclusive) {
  session_->EnqueueFrame(std::make_unique<SpdyPriorityIR>(
      stream_id, parent_stream_id, weight, exclusive));
}

void OgHttp2Adapter::SubmitPing(Http2PingId ping_id) {
  session_->EnqueueFrame(std::make_unique<SpdyPingIR>(ping_id));
}

void OgHttp2Adapter::SubmitShutdownNotice() {
  session_->StartGracefulShutdown();
}

void OgHttp2Adapter::SubmitGoAway(Http2StreamId last_accepted_stream_id,
                                  Http2ErrorCode error_code,
                                  absl::string_view opaque_data) {
  session_->EnqueueFrame(std::make_unique<SpdyGoAwayIR>(
      last_accepted_stream_id, TranslateErrorCode(error_code),
      std::string(opaque_data)));
}
void OgHttp2Adapter::SubmitWindowUpdate(Http2StreamId stream_id,
                                        int window_increment) {
  session_->EnqueueFrame(
      std::make_unique<SpdyWindowUpdateIR>(stream_id, window_increment));
}

void OgHttp2Adapter::SubmitMetadata(Http2StreamId stream_id,
                                    size_t /* max_frame_size */,
                                    std::unique_ptr<MetadataSource> source) {
  // Not necessary to pass max_frame_size along, since OgHttp2Session tracks the
  // peer's advertised max frame size.
  session_->SubmitMetadata(stream_id, std::move(source));
}

int OgHttp2Adapter::Send() { return session_->Send(); }

int OgHttp2Adapter::GetSendWindowSize() const {
  return session_->GetRemoteWindowSize();
}

int OgHttp2Adapter::GetStreamSendWindowSize(Http2StreamId stream_id) const {
  return session_->GetStreamSendWindowSize(stream_id);
}

int OgHttp2Adapter::GetStreamReceiveWindowLimit(Http2StreamId stream_id) const {
  return session_->GetStreamReceiveWindowLimit(stream_id);
}

int OgHttp2Adapter::GetStreamReceiveWindowSize(Http2StreamId stream_id) const {
  return session_->GetStreamReceiveWindowSize(stream_id);
}

int OgHttp2Adapter::GetReceiveWindowSize() const {
  return session_->GetReceiveWindowSize();
}

int OgHttp2Adapter::GetHpackEncoderDynamicTableSize() const {
  return session_->GetHpackEncoderDynamicTableSize();
}

int OgHttp2Adapter::GetHpackEncoderDynamicTableCapacity() const {
  return session_->GetHpackEncoderDynamicTableCapacity();
}

int OgHttp2Adapter::GetHpackDecoderDynamicTableSize() const {
  return session_->GetHpackDecoderDynamicTableSize();
}

int OgHttp2Adapter::GetHpackDecoderSizeLimit() const {
  return session_->GetHpackDecoderSizeLimit();
}

Http2StreamId OgHttp2Adapter::GetHighestReceivedStreamId() const {
  return session_->GetHighestReceivedStreamId();
}

void OgHttp2Adapter::MarkDataConsumedForStream(Http2StreamId stream_id,
                                               size_t num_bytes) {
  session_->Consume(stream_id, num_bytes);
}

void OgHttp2Adapter::SubmitRst(Http2StreamId stream_id,
                               Http2ErrorCode error_code) {
  session_->EnqueueFrame(std::make_unique<spdy::SpdyRstStreamIR>(
      stream_id, TranslateErrorCode(error_code)));
}

int32_t OgHttp2Adapter::SubmitRequest(
    absl::Span<const Header> headers,
    std::unique_ptr<DataFrameSource> data_source, void* user_data) {
  return session_->SubmitRequest(headers, std::move(data_source), user_data);
}

int OgHttp2Adapter::SubmitResponse(
    Http2StreamId stream_id, absl::Span<const Header> headers,
    std::unique_ptr<DataFrameSource> data_source) {
  return session_->SubmitResponse(stream_id, headers, std::move(data_source));
}

int OgHttp2Adapter::SubmitTrailer(Http2StreamId stream_id,
                                  absl::Span<const Header> trailers) {
  return session_->SubmitTrailer(stream_id, trailers);
}

void OgHttp2Adapter::SetStreamUserData(Http2StreamId stream_id,
                                       void* user_data) {
  session_->SetStreamUserData(stream_id, user_data);
}

void* OgHttp2Adapter::GetStreamUserData(Http2StreamId stream_id) {
  return session_->GetStreamUserData(stream_id);
}

bool OgHttp2Adapter::ResumeStream(Http2StreamId stream_id) {
  return session_->ResumeStream(stream_id);
}

OgHttp2Adapter::OgHttp2Adapter(Http2VisitorInterface& visitor, Options options)
    : Http2Adapter(visitor),
      session_(std::make_unique<OgHttp2Session>(visitor, std::move(options))) {}

}  // namespace adapter
}  // namespace http2
