#ifndef QUICHE_HTTP2_ADAPTER_HTTP2_ADAPTER_H_
#define QUICHE_HTTP2_ADAPTER_HTTP2_ADAPTER_H_

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "http2/adapter/http2_protocol.h"
#include "http2/adapter/http2_session.h"
#include "http2/adapter/http2_visitor_interface.h"

namespace http2 {
namespace adapter {

// Http2Adapter is an HTTP/2-processing class that exposes an interface similar
// to the nghttp2 library for processing the HTTP/2 wire format. As nghttp2
// parses HTTP/2 frames and invokes callbacks on Http2Adapter, Http2Adapter then
// invokes corresponding callbacks on its passed-in Http2VisitorInterface.
// Http2Adapter is a base class shared between client-side and server-side
// implementations.
class Http2Adapter {
 public:
  Http2Adapter(const Http2Adapter&) = delete;
  Http2Adapter& operator=(const Http2Adapter&) = delete;

  // Processes the incoming |bytes| as HTTP/2 and invokes callbacks on the
  // |visitor_| as appropriate.
  virtual ssize_t ProcessBytes(absl::string_view bytes) = 0;

  // Submits the |settings| to be written to the peer, e.g., as part of the
  // HTTP/2 connection preface.
  virtual void SubmitSettings(absl::Span<const Http2Setting> settings) = 0;

  // Submits a PRIORITY frame for the given stream.
  virtual void SubmitPriorityForStream(Http2StreamId stream_id,
                                       Http2StreamId parent_stream_id,
                                       int weight,
                                       bool exclusive) = 0;

  // Submits a PING on the connection. Note that nghttp2 automatically submits
  // PING acks upon receiving non-ack PINGs from the peer, so callers only use
  // this method to originate PINGs. See nghttp2_option_set_no_auto_ping_ack().
  virtual void SubmitPing(Http2PingId ping_id) = 0;

  // Submits a GOAWAY on the connection. Note that |last_accepted_stream_id|
  // refers to stream IDs initiated by the peer. For client-side, this last
  // stream ID must be even (or 0); for server-side, this last stream ID must be
  // odd (or 0). To submit a GOAWAY with |last_accepted_stream_id| with the
  // maximum stream ID, signaling imminent connection termination, call
  // SubmitShutdownNotice() instead (though this is only possible server-side).
  virtual void SubmitGoAway(Http2StreamId last_accepted_stream_id,
                            Http2ErrorCode error_code,
                            absl::string_view opaque_data) = 0;

  // Submits a WINDOW_UPDATE for the given stream (a |stream_id| of 0 indicates
  // a connection-level WINDOW_UPDATE).
  virtual void SubmitWindowUpdate(Http2StreamId stream_id,
                                  int window_increment) = 0;

  // Submits a METADATA frame for the given stream (a |stream_id| of 0 indicates
  // connection-level METADATA). If |fin|, the frame will also have the
  // END_METADATA flag set.
  virtual void SubmitMetadata(Http2StreamId stream_id, bool fin) = 0;

  // Returns serialized bytes for writing to the wire.
  // Writes should be submitted to Http2Adapter first, so that Http2Adapter
  // has data to serialize and return in this method.
  virtual std::string GetBytesToWrite(absl::optional<size_t> max_bytes) = 0;

  // Returns the connection-level flow control window for the peer.
  virtual int GetPeerConnectionWindow() const = 0;

  // Marks the given amount of data as consumed for the given stream, which
  // enables the nghttp2 layer to trigger WINDOW_UPDATEs as appropriate.
  virtual void MarkDataConsumedForStream(Http2StreamId stream_id,
                                         size_t num_bytes) = 0;

  // Submits a RST_STREAM for the given stream.
  virtual void SubmitRst(Http2StreamId stream_id,
                         Http2ErrorCode error_code) = 0;

 protected:
  // Subclasses should expose a public factory method for constructing and
  // initializing (via Initialize()) adapter instances.
  explicit Http2Adapter(Http2VisitorInterface& visitor) : visitor_(visitor) {}
  virtual ~Http2Adapter() {}

  // Accessors. Do not transfer ownership.
  Http2VisitorInterface& visitor() { return visitor_; }

 private:
  // Http2Adapter will invoke callbacks upon the |visitor_| while processing.
  Http2VisitorInterface& visitor_;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_HTTP2_ADAPTER_H_
