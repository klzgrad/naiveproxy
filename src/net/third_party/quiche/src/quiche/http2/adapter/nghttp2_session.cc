#include "quiche/http2/adapter/nghttp2_session.h"

#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {
namespace adapter {

NgHttp2Session::NgHttp2Session(Perspective perspective,
                               nghttp2_session_callbacks_unique_ptr callbacks,
                               const nghttp2_option* options, void* userdata)
    : session_(MakeSessionPtr(nullptr)), perspective_(perspective) {
  nghttp2_session* session;
  switch (perspective_) {
    case Perspective::kClient:
      nghttp2_session_client_new2(&session, callbacks.get(), userdata, options);
      break;
    case Perspective::kServer:
      nghttp2_session_server_new2(&session, callbacks.get(), userdata, options);
      break;
  }
  session_ = MakeSessionPtr(session);
}

NgHttp2Session::~NgHttp2Session() {
  // Can't invoke want_read() or want_write(), as they are virtual methods.
  const bool pending_reads = nghttp2_session_want_read(session_.get()) != 0;
  const bool pending_writes = nghttp2_session_want_write(session_.get()) != 0;
  if (pending_reads || pending_writes) {
    QUICHE_VLOG(1) << "Shutting down connection with pending reads: "
                   << pending_reads << " or pending writes: " << pending_writes;
  }
}

int64_t NgHttp2Session::ProcessBytes(absl::string_view bytes) {
  return nghttp2_session_mem_recv(
      session_.get(), reinterpret_cast<const uint8_t*>(bytes.data()),
      bytes.size());
}

int NgHttp2Session::Consume(Http2StreamId stream_id, size_t num_bytes) {
  return nghttp2_session_consume(session_.get(), stream_id, num_bytes);
}

bool NgHttp2Session::want_read() const {
  return nghttp2_session_want_read(session_.get()) != 0;
}

bool NgHttp2Session::want_write() const {
  return nghttp2_session_want_write(session_.get()) != 0;
}

int NgHttp2Session::GetRemoteWindowSize() const {
  return nghttp2_session_get_remote_window_size(session_.get());
}

}  // namespace adapter
}  // namespace http2
