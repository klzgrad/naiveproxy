#ifndef QUICHE_HTTP2_ADAPTER_HTTP2_ADAPTER_H_
#define QUICHE_HTTP2_ADAPTER_HTTP2_ADAPTER_H_

#include <cstdint>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "quiche/http2/adapter/data_source.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/http2_session.h"
#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace adapter {

// Http2Adapter is an HTTP/2-processing class that exposes an interface similar
// to the nghttp2 library for processing the HTTP/2 wire format. As nghttp2
// parses HTTP/2 frames and invokes callbacks on Http2Adapter, Http2Adapter then
// invokes corresponding callbacks on its passed-in Http2VisitorInterface.
// Http2Adapter is a base class shared between client-side and server-side
// implementations.
class QUICHE_EXPORT Http2Adapter {
 public:
  Http2Adapter(const Http2Adapter&) = delete;
  Http2Adapter& operator=(const Http2Adapter&) = delete;

  virtual ~Http2Adapter() {}

  virtual bool IsServerSession() const = 0;

  virtual bool want_read() const = 0;
  virtual bool want_write() const = 0;

  // Processes the incoming |bytes| as HTTP/2 and invokes callbacks on the
  // |visitor_| as appropriate.
  virtual int64_t ProcessBytes(absl::string_view bytes) = 0;

  // Submits the |settings| to be written to the peer, e.g., as part of the
  // HTTP/2 connection preface.
  virtual void SubmitSettings(absl::Span<const Http2Setting> settings) = 0;

  // Submits a PRIORITY frame for the given stream.
  virtual void SubmitPriorityForStream(Http2StreamId stream_id,
                                       Http2StreamId parent_stream_id,
                                       int weight, bool exclusive) = 0;

  // Submits a PING on the connection.
  virtual void SubmitPing(Http2PingId ping_id) = 0;

  // Starts a graceful shutdown. A no-op for clients.
  virtual void SubmitShutdownNotice() = 0;

  // Submits a GOAWAY on the connection. Note that |last_accepted_stream_id|
  // refers to stream IDs initiated by the peer. For a server sending this
  // frame, this last stream ID must be odd (or 0).
  virtual void SubmitGoAway(Http2StreamId last_accepted_stream_id,
                            Http2ErrorCode error_code,
                            absl::string_view opaque_data) = 0;

  // Submits a WINDOW_UPDATE for the given stream (a |stream_id| of 0 indicates
  // a connection-level WINDOW_UPDATE).
  virtual void SubmitWindowUpdate(Http2StreamId stream_id,
                                  int window_increment) = 0;

  // Submits a RST_STREAM for the given |stream_id| and |error_code|.
  virtual void SubmitRst(Http2StreamId stream_id,
                         Http2ErrorCode error_code) = 0;

  // Submits a sequence of METADATA frames for the given stream. A |stream_id|
  // of 0 indicates connection-level METADATA.
  virtual void SubmitMetadata(Http2StreamId stream_id, size_t max_frame_size,
                              std::unique_ptr<MetadataSource> source) = 0;

  // Invokes the visitor's OnReadyToSend() method for serialized frame data.
  // Returns 0 on success.
  virtual int Send() = 0;

  // Returns the connection-level flow control window advertised by the peer.
  virtual int GetSendWindowSize() const = 0;

  // Returns the stream-level flow control window advertised by the peer.
  virtual int GetStreamSendWindowSize(Http2StreamId stream_id) const = 0;

  // Returns the current upper bound on the flow control receive window for this
  // stream. This value does not account for data received from the peer.
  virtual int GetStreamReceiveWindowLimit(Http2StreamId stream_id) const = 0;

  // Returns the amount of data a peer could send on a given stream. This is
  // the outstanding stream receive window.
  virtual int GetStreamReceiveWindowSize(Http2StreamId stream_id) const = 0;

  // Returns the total amount of data a peer could send on the connection. This
  // is the outstanding connection receive window.
  virtual int GetReceiveWindowSize() const = 0;

  // Returns the size of the HPACK encoder's dynamic table, including the
  // per-entry overhead from the specification.
  virtual int GetHpackEncoderDynamicTableSize() const = 0;

  // Returns the size of the HPACK decoder's dynamic table, including the
  // per-entry overhead from the specification.
  virtual int GetHpackDecoderDynamicTableSize() const = 0;

  // Gets the highest stream ID value seen in a frame received by this endpoint.
  // This method is only guaranteed to work for server endpoints.
  virtual Http2StreamId GetHighestReceivedStreamId() const = 0;

  // Marks the given amount of data as consumed for the given stream, which
  // enables the implementation layer to send WINDOW_UPDATEs as appropriate.
  virtual void MarkDataConsumedForStream(Http2StreamId stream_id,
                                         size_t num_bytes) = 0;

  // Returns the assigned stream ID if the operation succeeds. Otherwise,
  // returns a negative integer indicating an error code. |data_source| may be
  // nullptr if the request does not have a body.
  virtual int32_t SubmitRequest(absl::Span<const Header> headers,
                                std::unique_ptr<DataFrameSource> data_source,
                                void* user_data) = 0;

  // Returns 0 on success. |data_source| may be nullptr if the response does not
  // have a body.
  virtual int SubmitResponse(Http2StreamId stream_id,
                             absl::Span<const Header> headers,
                             std::unique_ptr<DataFrameSource> data_source) = 0;

  // Queues trailers to be sent after any outstanding data on the stream with ID
  // |stream_id|. Returns 0 on success.
  virtual int SubmitTrailer(Http2StreamId stream_id,
                            absl::Span<const Header> trailers) = 0;

  // Sets a user data pointer for the given stream. Can be called after
  // SubmitRequest/SubmitResponse, or after receiving any frame for a given
  // stream.
  virtual void SetStreamUserData(Http2StreamId stream_id, void* user_data) = 0;

  // Returns nullptr if the stream does not exist, or if stream user data has
  // not been set.
  virtual void* GetStreamUserData(Http2StreamId stream_id) = 0;

  // Resumes a stream that was previously blocked (for example, due to
  // DataFrameSource::SelectPayloadLength() returning kBlocked). Returns true if
  // the stream was successfully resumed.
  virtual bool ResumeStream(Http2StreamId stream_id) = 0;

 protected:
  // Subclasses should expose a public factory method for constructing and
  // initializing (via Initialize()) adapter instances.
  explicit Http2Adapter(Http2VisitorInterface& visitor) : visitor_(visitor) {}

  // Accessors. Do not transfer ownership.
  Http2VisitorInterface& visitor() { return visitor_; }

 private:
  // Http2Adapter will invoke callbacks upon the |visitor_| while processing.
  Http2VisitorInterface& visitor_;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_HTTP2_ADAPTER_H_
