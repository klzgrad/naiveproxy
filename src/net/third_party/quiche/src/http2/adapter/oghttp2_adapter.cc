#include "http2/adapter/oghttp2_adapter.h"

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "http2/adapter/http2_util.h"
#include "common/platform/api/quiche_bug_tracker.h"
#include "spdy/core/spdy_protocol.h"

namespace http2 {
namespace adapter {

namespace {

using spdy::SpdyFrameIR;
using spdy::SpdyGoAwayIR;
using spdy::SpdyPingIR;
using spdy::SpdyPriorityIR;
using spdy::SpdySettingsIR;
using spdy::SpdyWindowUpdateIR;

}  // namespace

/* static */
std::unique_ptr<OgHttp2Adapter> OgHttp2Adapter::Create(
    Http2VisitorInterface& visitor,
    Options options) {
  // Using `new` to access a non-public constructor.
  return absl::WrapUnique(new OgHttp2Adapter(visitor, std::move(options)));
}

OgHttp2Adapter::~OgHttp2Adapter() {}

ssize_t OgHttp2Adapter::ProcessBytes(absl::string_view bytes) {
  return session_->ProcessBytes(bytes);
}

void OgHttp2Adapter::SubmitSettings(absl::Span<const Http2Setting> settings) {
  auto settings_ir = absl::make_unique<SpdySettingsIR>();
  for (const Http2Setting& setting : settings) {
    settings_ir->AddSetting(setting.id, setting.value);
  }
  session_->EnqueueFrame(std::move(settings_ir));
}

void OgHttp2Adapter::SubmitPriorityForStream(Http2StreamId stream_id,
                                             Http2StreamId parent_stream_id,
                                             int weight,
                                             bool exclusive) {
  session_->EnqueueFrame(absl::make_unique<SpdyPriorityIR>(
      stream_id, parent_stream_id, weight, exclusive));
}

void OgHttp2Adapter::SubmitPing(Http2PingId ping_id) {
  session_->EnqueueFrame(absl::make_unique<SpdyPingIR>(ping_id));
}

void OgHttp2Adapter::SubmitGoAway(Http2StreamId last_accepted_stream_id,
                                  Http2ErrorCode error_code,
                                  absl::string_view opaque_data) {
  session_->EnqueueFrame(absl::make_unique<SpdyGoAwayIR>(
      last_accepted_stream_id, TranslateErrorCode(error_code),
      std::string(opaque_data)));
}
void OgHttp2Adapter::SubmitWindowUpdate(Http2StreamId stream_id,
                                        int window_increment) {
  session_->EnqueueFrame(
      absl::make_unique<SpdyWindowUpdateIR>(stream_id, window_increment));
}

void OgHttp2Adapter::SubmitMetadata(Http2StreamId stream_id, bool fin) {
  QUICHE_BUG(oghttp2_submit_metadata) << "Not implemented";
}

std::string OgHttp2Adapter::GetBytesToWrite(absl::optional<size_t> max_bytes) {
  return session_->GetBytesToWrite(max_bytes);
}

int OgHttp2Adapter::GetPeerConnectionWindow() const {
  return session_->GetRemoteWindowSize();
}

void OgHttp2Adapter::MarkDataConsumedForStream(Http2StreamId stream_id,
                                               size_t num_bytes) {
  session_->Consume(stream_id, num_bytes);
}

void OgHttp2Adapter::SubmitRst(Http2StreamId stream_id,
                               Http2ErrorCode error_code) {
  session_->EnqueueFrame(absl::make_unique<spdy::SpdyRstStreamIR>(
      stream_id, TranslateErrorCode(error_code)));
}

const Http2Session& OgHttp2Adapter::session() const {
  return *session_;
}

OgHttp2Adapter::OgHttp2Adapter(Http2VisitorInterface& visitor, Options options)
    : Http2Adapter(visitor),
      session_(absl::make_unique<OgHttp2Session>(visitor, std::move(options))) {
}

}  // namespace adapter
}  // namespace http2
