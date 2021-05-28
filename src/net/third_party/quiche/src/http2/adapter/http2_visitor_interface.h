#ifndef QUICHE_HTTP2_ADAPTER_HTTP2_VISITOR_INTERFACE_H_
#define QUICHE_HTTP2_ADAPTER_HTTP2_VISITOR_INTERFACE_H_

#include <vector>

#include "absl/strings/string_view.h"
#include "http2/adapter/http2_protocol.h"

namespace http2 {
namespace adapter {

// Http2VisitorInterface contains callbacks for receiving HTTP/2-level events. A
// processor like NghttpAdapter parses HTTP/2 frames and invokes the callbacks
// on an instance of this interface. Prefer a void return type for these
// callbacks, instead setting output parameters as needed.
//
// Example sequences of calls/events:
//   GET:
//     - OnBeginHeadersForStream()
//     - OnHeaderForStream()
//     - OnEndHeadersForStream()
//     - OnEndStream()
//
//   POST:
//     - OnBeginHeadersForStream()
//     - OnHeaderForStream()
//     - OnEndHeadersForStream()
//     - OnBeginDataForStream()
//     - OnDataForStream()
//     - OnEndStream()
//
//   Request canceled mid-stream, e.g, with error code CANCEL:
//     - OnBeginHeadersForStream()
//     - OnHeaderForStream()
//     - OnEndHeadersForStream()
//     - OnRstStream()
//     - OnAbortStream()
//
//   Request closed mid-stream, e.g., with error code NO_ERROR:
//     - OnBeginHeadersForStream()
//     - OnHeaderForStream()
//     - OnEndHeadersForStream()
//     - OnRstStream()
//     - OnCloseStream()
//
// More details are at RFC 7540 (go/http2spec), and more examples are at
// http://google3/net/http2/server/lib/internal/h2/nghttp2/nghttp2_server_adapter_test.cc.
class Http2VisitorInterface {
 public:
  Http2VisitorInterface(const Http2VisitorInterface&) = delete;
  Http2VisitorInterface& operator=(const Http2VisitorInterface&) = delete;
  virtual ~Http2VisitorInterface() = default;

  // Called when a connection-level processing error has been encountered.
  virtual void OnConnectionError() = 0;

  // Called when a non-ack SETTINGS frame is received.
  virtual void OnSettingsStart() = 0;

  // Called for each SETTINGS id-value pair.
  virtual void OnSetting(Http2Setting setting) = 0;

  // Called at the end of a non-ack SETTINGS frame.
  virtual void OnSettingsEnd() = 0;

  // Called when a SETTINGS ack frame is received.
  virtual void OnSettingsAck() = 0;

  // Called when the connection receives the header block for a HEADERS frame on
  // a stream but has not yet parsed individual headers.
  virtual void OnBeginHeadersForStream(Http2StreamId stream_id) = 0;

  // Called when the connection receives the header |key| and |value| for a
  // stream. The HTTP/2 pseudo-headers defined in RFC 7540 Sections 8.1.2.3 and
  // 8.1.2.4 are also conveyed in this callback. This method is called after
  // OnBeginHeadersForStream().
  virtual void OnHeaderForStream(Http2StreamId stream_id, absl::string_view key,
                                 absl::string_view value) = 0;

  // Called when the connection has received the complete header block for a
  // logical HEADERS frame on a stream (which may contain CONTINUATION frames,
  // transparent to the user).
  virtual void OnEndHeadersForStream(Http2StreamId stream_id) = 0;

  // Called when the connection receives the beginning of a DATA frame. The data
  // payload will be provided via subsequent calls to OnDataForStream().
  virtual void OnBeginDataForStream(Http2StreamId stream_id,
                                    size_t payload_length) = 0;

  // Called when the connection receives some |data| (as part of a DATA frame
  // payload) for a stream.
  virtual void OnDataForStream(Http2StreamId stream_id,
                               absl::string_view data) = 0;

  // Called when the peer sends the END_STREAM flag on a stream, indicating that
  // the peer will not send additional headers or data for that stream.
  virtual void OnEndStream(Http2StreamId stream_id) = 0;

  // Called when the connection receives a RST_STREAM for a stream. This call
  // will be followed by either OnCloseStream() or OnAbortStream().
  virtual void OnRstStream(Http2StreamId stream_id,
                           Http2ErrorCode error_code) = 0;

  // Called when a stream is closed with error code NO_ERROR. Compare with
  // OnAbortStream().
  virtual void OnCloseStream(Http2StreamId stream_id) = 0;

  // Called when a stream is aborted, i.e., closed for the reason indicated by
  // the given |error_code|, where error_code != NO_ERROR. Compare with
  // OnCloseStream().
  virtual void OnAbortStream(Http2StreamId stream_id,
                             Http2ErrorCode error_code) = 0;

  // Called when the connection receives a PRIORITY frame.
  virtual void OnPriorityForStream(Http2StreamId stream_id,
                                   Http2StreamId parent_stream_id, int weight,
                                   bool exclusive) = 0;

  // Called when the connection receives a PING frame.
  virtual void OnPing(Http2PingId ping_id, bool is_ack) = 0;

  // Called when the connection receives a PUSH_PROMISE frame. The server push
  // request headers follow in calls to OnHeaderForStream() with |stream_id|.
  virtual void OnPushPromiseForStream(Http2StreamId stream_id,
                                      Http2StreamId promised_stream_id) = 0;

  // Called when the connection receives a GOAWAY frame.
  virtual void OnGoAway(Http2StreamId last_accepted_stream_id,
                        Http2ErrorCode error_code,
                        absl::string_view opaque_data) = 0;

  // Called when the connection receives a WINDOW_UPDATE frame. For
  // connection-level window updates, the |stream_id| will be 0.
  virtual void OnWindowUpdate(Http2StreamId stream_id,
                              int window_increment) = 0;

  // Called when the connection is ready to send data for a stream. The
  // implementation should write at most |length| bytes of the data payload to
  // the |destination_buffer| and set |end_stream| to true IFF there will be no
  // more data sent on this stream. Sets |written| to the number of bytes
  // written to the |destination_buffer| or a negative value if an error occurs.
  virtual void OnReadyToSendDataForStream(Http2StreamId stream_id,
                                             char* destination_buffer,
                                             size_t length,
                                             ssize_t* written,
                                             bool* end_stream) = 0;

  // Called when the connection is ready to write metadata for |stream_id| to
  // the wire. The implementation should write at most |length| bytes of the
  // serialized metadata payload to the |buffer| and set |written| to the number
  // of bytes written or a negative value if there was an error.
  virtual void OnReadyToSendMetadataForStream(Http2StreamId stream_id,
                                              char* buffer, size_t length,
                                              ssize_t* written) = 0;

  // Called when the connection receives the beginning of a METADATA frame
  // (which may itself be the middle of a logical metadata block). The metadata
  // payload will be provided via subsequent calls to OnMetadataForStream().
  virtual void OnBeginMetadataForStream(Http2StreamId stream_id,
                                        size_t payload_length) = 0;

  // Called when the connection receives |metadata| as part of a METADATA frame
  // payload for a stream.
  virtual void OnMetadataForStream(Http2StreamId stream_id,
                                   absl::string_view metadata) = 0;

  // Called when the connection has finished receiving a logical metadata block
  // for a stream. Note that there may be multiple metadata blocks for a stream.
  virtual void OnMetadataEndForStream(Http2StreamId stream_id) = 0;

 protected:
  Http2VisitorInterface() = default;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_HTTP2_VISITOR_INTERFACE_H_
