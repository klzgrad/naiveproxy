#ifndef QUICHE_HTTP2_ADAPTER_NGHTTP2_CALLBACKS_H_
#define QUICHE_HTTP2_ADAPTER_NGHTTP2_CALLBACKS_H_

#include "http2/adapter/http2_protocol.h"
#include "http2/adapter/nghttp2_util.h"
#include "third_party/nghttp2/src/lib/includes/nghttp2/nghttp2.h"

namespace http2 {
namespace adapter {
namespace callbacks {

// The following functions are nghttp2 callbacks that Nghttp2Adapter sets at the
// beginning of its lifetime. It is expected that |user_data| holds an
// Http2VisitorInterface.

// Callback once a frame header has been received.
int OnBeginFrame(nghttp2_session* session, const nghttp2_frame_hd* header,
                 void* user_data);

// Callback once a complete frame has been received.
int OnFrameReceived(nghttp2_session* session, const nghttp2_frame* frame,
                    void* user_data);

// Callback at the start of a frame carrying headers.
int OnBeginHeaders(nghttp2_session* session,
                   const nghttp2_frame* frame,
                   void* user_data);

// Callback once a name-value header has been received.
int OnHeader(nghttp2_session* session, const nghttp2_frame* frame,
             nghttp2_rcbuf* name, nghttp2_rcbuf* value, uint8_t flags,
             void* user_data);

// Callback once a chunk of data (from a DATA frame payload) has been received.
int OnDataChunk(nghttp2_session* session, uint8_t flags,
                Http2StreamId stream_id, const uint8_t* data, size_t len,
                void* user_data);

// Callback once a stream has been closed.
int OnStreamClosed(nghttp2_session* session, Http2StreamId stream_id,
                   uint32_t error_code, void* user_data);

// Callback once nghttp2 is ready to read data from |source| into |dest_buffer|.
ssize_t OnReadyToReadDataForStream(nghttp2_session* session,
                                   Http2StreamId stream_id,
                                   uint8_t* dest_buffer, size_t max_length,
                                   uint32_t* data_flags,
                                   nghttp2_data_source* source,
                                   void* user_data);

nghttp2_session_callbacks_unique_ptr Create();

}  // namespace callbacks
}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_NGHTTP2_CALLBACKS_H_
