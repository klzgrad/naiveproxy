#ifndef QUICHE_HTTP2_ADAPTER_HTTP2_VISITOR_INTERFACE_H_
#define QUICHE_HTTP2_ADAPTER_HTTP2_VISITOR_INTERFACE_H_

#include <cstdint>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_logging.h"

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
//     - OnCloseStream()
//
//   Request closed mid-stream, e.g., with error code NO_ERROR:
//     - OnBeginHeadersForStream()
//     - OnHeaderForStream()
//     - OnEndHeadersForStream()
//     - OnRstStream()
//     - OnCloseStream()
//
// More details are at RFC 7540 (go/http2spec).
class QUICHE_EXPORT Http2VisitorInterface {
 public:
  Http2VisitorInterface(const Http2VisitorInterface&) = delete;
  Http2VisitorInterface& operator=(const Http2VisitorInterface&) = delete;
  virtual ~Http2VisitorInterface() = default;

  enum : int64_t {
    kSendBlocked = 0,
    kSendError = -1,
  };
  // Called when there are serialized frames to send. Should return how many
  // bytes were actually sent. May return kSendBlocked or kSendError.
  virtual int64_t OnReadyToSend(absl::string_view serialized) = 0;

  struct DataFrameHeaderInfo {
    int64_t payload_length;
    bool end_data;
    bool end_stream;  // If true, also implies end_data.
  };
  // Called when the codec is ready to construct a DATA frame header. The
  // implementation should return the number of bytes ready to send, and whether
  // it's the end of the message body data. If the implementation returns 0
  // bytes and also `end_data` is false, then the stream is deferred until
  // resumed by the application. `payload_length` must be at most `max_length`.
  // A `payload_length` of -1 indicates that this stream has encountered an
  // unrecoverable error.
  virtual DataFrameHeaderInfo OnReadyToSendDataForStream(
      Http2StreamId /*stream_id*/, size_t /*max_length*/) {
    QUICHE_LOG(FATAL) << "Not implemented";
    return DataFrameHeaderInfo{};
  }

  // Called when the codec is ready to send a DATA frame. The implementation
  // should send the `frame_header` and specified number of payload bytes.
  // Returning false indicates an unrecoverable error.
  virtual bool SendDataFrame(Http2StreamId /*stream_id*/,
                             absl::string_view /*frame_header*/,
                             size_t /*payload_bytes*/) {
    QUICHE_LOG(FATAL) << "Not implemented.";
    return false;
  }

  // Called when a connection-level error has occurred.
  enum class ConnectionError {
    // The peer sent an invalid connection preface.
    kInvalidConnectionPreface,
    // The visitor encountered an error sending bytes to the peer.
    kSendError,
    // There was an error reading and framing bytes from the peer.
    kParseError,
    // The visitor considered a received header to be a connection error.
    kHeaderError,
    // The peer attempted to open a stream with an invalid stream ID.
    kInvalidNewStreamId,
    // The peer sent a frame that is invalid on an idle stream (before HEADERS).
    kWrongFrameSequence,
    // The peer sent an invalid PUSH_PROMISE frame.
    kInvalidPushPromise,
    // The peer exceeded the max concurrent streams limit.
    kExceededMaxConcurrentStreams,
    // The peer caused a flow control error.
    kFlowControlError,
    // The peer sent a GOAWAY with an invalid last-stream-ID field.
    kInvalidGoAwayLastStreamId,
    // The peer sent an invalid SETTINGS value.
    kInvalidSetting,
  };
  virtual void OnConnectionError(ConnectionError error) = 0;

  // Called when the header for a frame is received. Returns false if a fatal
  // error has occurred.
  virtual bool OnFrameHeader(Http2StreamId /*stream_id*/, size_t /*length*/,
                             uint8_t /*type*/, uint8_t /*flags*/) {
    return true;
  }

  // Called when a non-ack SETTINGS frame is received.
  virtual void OnSettingsStart() = 0;

  // Called for each SETTINGS id-value pair.
  virtual void OnSetting(Http2Setting setting) = 0;

  // Called at the end of a non-ack SETTINGS frame.
  virtual void OnSettingsEnd() = 0;

  // Called when a SETTINGS ack frame is received.
  virtual void OnSettingsAck() = 0;

  // Called when the connection receives the header block for a HEADERS frame on
  // a stream but has not yet parsed individual headers. Returns false if a
  // fatal error has occurred.
  virtual bool OnBeginHeadersForStream(Http2StreamId stream_id) = 0;

  // Called when the connection receives the header |key| and |value| for a
  // stream. The HTTP/2 pseudo-headers defined in RFC 7540 Sections 8.1.2.3 and
  // 8.1.2.4 are also conveyed in this callback. This method is called after
  // OnBeginHeadersForStream(). May return HEADER_RST_STREAM to indicate the
  // header block should be rejected. This will cause the library to queue a
  // RST_STREAM frame, which will have a default error code of INTERNAL_ERROR.
  // The visitor implementation may choose to queue a RST_STREAM with a
  // different error code instead, which should be done before returning
  // HEADER_RST_STREAM. Returning HEADER_CONNECTION_ERROR will lead to a
  // non-recoverable error on the connection.
  enum OnHeaderResult {
    // The header was accepted.
    HEADER_OK,
    // The application considers the header a connection error.
    HEADER_CONNECTION_ERROR,
    // The application rejects the header and requests the stream be reset.
    HEADER_RST_STREAM,
    // The header field is invalid and will be reset with error code
    // PROTOCOL_ERROR.
    HEADER_FIELD_INVALID,
    // The headers are a violation of HTTP messaging semantics and will be reset
    // with error code PROTOCOL_ERROR.
    HEADER_HTTP_MESSAGING,
    // The headers caused a compression context error.
    HEADER_COMPRESSION_ERROR,
  };
  virtual OnHeaderResult OnHeaderForStream(Http2StreamId stream_id,
                                           absl::string_view key,
                                           absl::string_view value) = 0;

  // Called when the connection has received the complete header block for a
  // logical HEADERS frame on a stream (which may contain CONTINUATION frames,
  // transparent to the user). Returns false if a fatal error has occurred.
  virtual bool OnEndHeadersForStream(Http2StreamId stream_id) = 0;

  // Called when the connection receives the beginning of a DATA frame. The data
  // payload will be provided via subsequent calls to OnDataForStream(). Returns
  // false if a fatal error has occurred.
  virtual bool OnBeginDataForStream(Http2StreamId stream_id,
                                    size_t payload_length) = 0;

  // Called when the optional padding length field is parsed as part of a DATA
  // frame payload. `padding_length` represents the total amount of padding for
  // this frame, including the length byte itself. Returns false if a fatal
  // error has occurred.
  virtual bool OnDataPaddingLength(Http2StreamId stream_id,
                                   size_t padding_length) = 0;

  // Called when the connection receives some |data| (as part of a DATA frame
  // payload) for a stream. Returns false if a fatal error has occurred.
  virtual bool OnDataForStream(Http2StreamId stream_id,
                               absl::string_view data) = 0;

  // Called when the peer sends the END_STREAM flag on a stream, indicating that
  // the peer will not send additional headers or data for that stream.
  virtual bool OnEndStream(Http2StreamId stream_id) = 0;

  // Called when the connection receives a RST_STREAM for a stream. This call
  // will be followed by either OnCloseStream().
  virtual void OnRstStream(Http2StreamId stream_id,
                           Http2ErrorCode error_code) = 0;

  // Called when a stream is closed. Returns false if a fatal error has
  // occurred.
  virtual bool OnCloseStream(Http2StreamId stream_id,
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

  // Called when the connection receives a GOAWAY frame. Returns false if a
  // fatal error has occurred.
  virtual bool OnGoAway(Http2StreamId last_accepted_stream_id,
                        Http2ErrorCode error_code,
                        absl::string_view opaque_data) = 0;

  // Called when the connection receives a WINDOW_UPDATE frame. For
  // connection-level window updates, the |stream_id| will be 0.
  virtual void OnWindowUpdate(Http2StreamId stream_id,
                              int window_increment) = 0;

  // Called immediately before a frame of the given type is sent. Should return
  // 0 on success.
  virtual int OnBeforeFrameSent(uint8_t frame_type, Http2StreamId stream_id,
                                size_t length, uint8_t flags) = 0;

  // Called immediately after a frame of the given type is sent. Should return 0
  // on success. |error_code| is only populated for RST_STREAM and GOAWAY frame
  // types.
  virtual int OnFrameSent(uint8_t frame_type, Http2StreamId stream_id,
                          size_t length, uint8_t flags,
                          uint32_t error_code) = 0;

  // Called when the connection receives an invalid frame. A return value of
  // false will result in the connection entering an error state, with no
  // further frame processing possible.
  enum class InvalidFrameError {
    // The frame contains a general protocol error.
    kProtocol,
    // The frame would have caused a new (invalid) stream to be opened.
    kRefusedStream,
    // The frame contains an invalid header field.
    kHttpHeader,
    // The frame contains a violation in HTTP messaging rules.
    kHttpMessaging,
    // The frame causes a flow control error.
    kFlowControl,
    // The frame is on an already closed stream or has an invalid stream ID.
    kStreamClosed,
  };
  virtual bool OnInvalidFrame(Http2StreamId stream_id,
                              InvalidFrameError error) = 0;

  // Called when the connection receives the beginning of a METADATA frame
  // (which may itself be the middle of a logical metadata block). The metadata
  // payload will be provided via subsequent calls to OnMetadataForStream().
  // TODO(birenroy): Consider removing this unnecessary method.
  virtual void OnBeginMetadataForStream(Http2StreamId stream_id,
                                        size_t payload_length) = 0;

  // Called when the connection receives |metadata| as part of a METADATA frame
  // payload for a stream. Returns false if a fatal error has occurred.
  virtual bool OnMetadataForStream(Http2StreamId stream_id,
                                   absl::string_view metadata) = 0;

  // Called when the connection has finished receiving a logical metadata block
  // for a stream. Note that there may be multiple metadata blocks for a stream.
  // Returns false if there was an error unpacking the metadata payload.
  virtual bool OnMetadataEndForStream(Http2StreamId stream_id) = 0;

  // Invoked with an error message from the application.
  virtual void OnErrorDebug(absl::string_view message) = 0;

 protected:
  Http2VisitorInterface() = default;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_HTTP2_VISITOR_INTERFACE_H_
