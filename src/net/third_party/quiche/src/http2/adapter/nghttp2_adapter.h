#ifndef QUICHE_HTTP2_ADAPTER_NGHTTP2_ADAPTER_H_
#define QUICHE_HTTP2_ADAPTER_NGHTTP2_ADAPTER_H_

#include "http2/adapter/http2_adapter.h"
#include "http2/adapter/http2_protocol.h"
#include "http2/adapter/nghttp2_session.h"
#include "http2/adapter/nghttp2_util.h"

namespace http2 {
namespace adapter {

class NgHttp2Adapter : public Http2Adapter {
 public:
  ~NgHttp2Adapter() override;

  // Creates an adapter that functions as a client.
  static std::unique_ptr<NgHttp2Adapter> CreateClientAdapter(
      Http2VisitorInterface& visitor);

  // Creates an adapter that functions as a server.
  static std::unique_ptr<NgHttp2Adapter> CreateServerAdapter(
      Http2VisitorInterface& visitor);

  // Processes the incoming |bytes| as HTTP/2 and invokes callbacks on the
  // |visitor_| as appropriate.
  ssize_t ProcessBytes(absl::string_view bytes) override;

  // Submits the |settings| to be written to the peer, e.g., as part of the
  // HTTP/2 connection preface.
  void SubmitSettings(absl::Span<const Http2Setting> settings) override;

  // Submits a PRIORITY frame for the given stream.
  void SubmitPriorityForStream(Http2StreamId stream_id,
                               Http2StreamId parent_stream_id,
                               int weight,
                               bool exclusive) override;

  // Submits a PING on the connection. Note that nghttp2 automatically submits
  // PING acks upon receiving non-ack PINGs from the peer, so callers only use
  // this method to originate PINGs. See nghttp2_option_set_no_auto_ping_ack().
  void SubmitPing(Http2PingId ping_id) override;

  // Submits a GOAWAY on the connection. Note that |last_accepted_stream_id|
  // refers to stream IDs initiated by the peer. For client-side, this last
  // stream ID must be even (or 0); for server-side, this last stream ID must be
  // odd (or 0).
  // TODO(birenroy): Add a graceful shutdown behavior to the API.
  void SubmitGoAway(Http2StreamId last_accepted_stream_id,
                    Http2ErrorCode error_code,
                    absl::string_view opaque_data) override;

  // Submits a WINDOW_UPDATE for the given stream (a |stream_id| of 0 indicates
  // a connection-level WINDOW_UPDATE).
  void SubmitWindowUpdate(Http2StreamId stream_id,
                          int window_increment) override;

  // Submits a METADATA frame for the given stream (a |stream_id| of 0 indicates
  // connection-level METADATA). If |end_metadata|, the frame will also have the
  // END_METADATA flag set.
  void SubmitMetadata(Http2StreamId stream_id, bool end_metadata) override;

  // Returns serialized bytes for writing to the wire. Writes should be
  // submitted to Nghttp2Adapter first, so that Nghttp2Adapter has data to
  // serialize and return in this method.
  std::string GetBytesToWrite(absl::optional<size_t> max_bytes) override;

  // Returns the connection-level flow control window for the peer.
  int GetPeerConnectionWindow() const override;

  // Marks the given amount of data as consumed for the given stream, which
  // enables the nghttp2 layer to trigger WINDOW_UPDATEs as appropriate.
  void MarkDataConsumedForStream(Http2StreamId stream_id,
                                 size_t num_bytes) override;

  // Submits a RST_STREAM with the desired |error_code|.
  void SubmitRst(Http2StreamId stream_id, Http2ErrorCode error_code) override;

  // TODO(b/181586191): Temporary accessor until equivalent functionality is
  // available in this adapter class.
  NgHttp2Session& session() { return *session_; }

 private:
  NgHttp2Adapter(Http2VisitorInterface& visitor, Perspective perspective);

  // Performs any necessary initialization of the underlying HTTP/2 session,
  // such as preparing initial SETTINGS.
  void Initialize();

  std::unique_ptr<NgHttp2Session> session_;
  Http2VisitorInterface& visitor_;
  Perspective perspective_;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_NGHTTP2_ADAPTER_H_
