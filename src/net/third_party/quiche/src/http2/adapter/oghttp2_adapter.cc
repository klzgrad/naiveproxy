#include "http2/adapter/oghttp2_adapter.h"

#include <list>

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "http2/adapter/http2_util.h"
#include "http2/adapter/window_manager.h"
#include "spdy/core/spdy_framer.h"
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

struct StreamState {
  WindowManager window_manager;
};

class OgHttp2Adapter::OgHttp2Session : public Http2Session {
 public:
  OgHttp2Session(Http2VisitorInterface& /*visitor*/, Options /*options*/) {}
  ~OgHttp2Session() override {}

  ssize_t ProcessBytes(absl::string_view bytes) override {
    SPDY_BUG(oghttp2_process_bytes) << "Not implemented";
    return 0;
  }

  int Consume(Http2StreamId stream_id, size_t num_bytes) override {
    auto it = stream_map_.find(stream_id);
    if (it == stream_map_.end()) {
      // TODO(b/181586191): LOG_ERROR rather than SPDY_BUG.
      SPDY_BUG(stream_consume_notfound)
          << "Stream " << stream_id << " not found";
    } else {
      it->second.window_manager.MarkDataFlushed(num_bytes);
    }
    return 0;  // Remove?
  }

  bool want_read() const override { return false; }
  bool want_write() const override {
    return !frames_.empty() || !serialized_prefix_.empty();
  }
  int GetRemoteWindowSize() const override {
    SPDY_BUG(peer_window_not_updated) << "Not implemented";
    return peer_window_;
  }

  void EnqueueFrame(std::unique_ptr<spdy::SpdyFrameIR> frame) {
    frames_.push_back(std::move(frame));
  }

  std::string GetBytesToWrite(absl::optional<size_t> max_bytes) {
    const size_t serialized_max =
        max_bytes ? max_bytes.value() : std::numeric_limits<size_t>::max();
    std::string serialized = std::move(serialized_prefix_);
    while (serialized.size() < serialized_max && !frames_.empty()) {
      spdy::SpdySerializedFrame frame =
          framer_.SerializeFrame(*frames_.front());
      absl::StrAppend(&serialized, absl::string_view(frame));
      frames_.pop_front();
    }
    if (serialized.size() > serialized_max) {
      serialized_prefix_ = serialized.substr(serialized_max);
      serialized.resize(serialized_max);
    }
    return serialized;
  }

 private:
  spdy::SpdyFramer framer_{spdy::SpdyFramer::ENABLE_COMPRESSION};
  absl::flat_hash_map<Http2StreamId, StreamState> stream_map_;
  std::list<std::unique_ptr<SpdyFrameIR>> frames_;
  std::string serialized_prefix_;
  int peer_window_ = 65535;
};

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
  SPDY_BUG(oghttp2_submit_metadata) << "Not implemented";
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

const Http2Session& OgHttp2Adapter::session() const {
  return *session_;
}

OgHttp2Adapter::OgHttp2Adapter(Http2VisitorInterface& visitor, Options options)
    : Http2Adapter(visitor),
      session_(absl::make_unique<OgHttp2Session>(visitor, std::move(options))) {
}

}  // namespace adapter
}  // namespace http2
