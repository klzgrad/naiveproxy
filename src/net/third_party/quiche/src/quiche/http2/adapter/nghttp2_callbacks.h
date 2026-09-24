#ifndef QUICHE_HTTP2_ADAPTER_NGHTTP2_CALLBACKS_H_
#define QUICHE_HTTP2_ADAPTER_NGHTTP2_CALLBACKS_H_

#include <cstdint>

#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/nghttp2.h"
#include "quiche/http2/adapter/nghttp2_util.h"

namespace http2 {
namespace adapter {
namespace callbacks {

// The following functions are nghttp2 callbacks that Nghttp2Adapter sets at the
// beginning of its lifetime. It is expected that |user_data| holds an
// Http2VisitorInterface.

// Callback once the library is ready to send serialized frames.
ssize_t OnReadyToSend(nghttp2_session* session, const uint8_t* data,
                      size_t length, int flags, void* user_data);

// Callback once a frame header has been received.
int OnBeginFrame(nghttp2_session* session, const nghttp2_frame_hd* header,
                 void* user_data);

// Callback once a complete frame has been received.
int OnFrameReceived(nghttp2_session* session, const nghttp2_frame* frame,
                    void* user_data);

// Callback at the start of a frame carrying headers.
int OnBeginHeaders(nghttp2_session* session, const nghttp2_frame* frame,
                   void* user_data);

// Callback once a name-value header has been received.
int OnHeader(nghttp2_session* session, const nghttp2_frame* frame,
             nghttp2_rcbuf* name, nghttp2_rcbuf* value, uint8_t flags,
             void* user_data);

// Invoked immediately before sending a frame.
int OnBeforeFrameSent(nghttp2_session* session, const nghttp2_frame* frame,
                      void* user_data);

// Invoked immediately after a frame is sent.
int OnFrameSent(nghttp2_session* session, const nghttp2_frame* frame,
                void* user_data);

// Invoked when a non-DATA frame is not sent because of an error.
int OnFrameNotSent(nghttp2_session* session, const nghttp2_frame* frame,
                   int lib_error_code, void* user_data);

// Invoked when an invalid frame is received.
int OnInvalidFrameReceived(nghttp2_session* session, const nghttp2_frame* frame,
                           int lib_error_code, void* user_data);

// Invoked when a chunk of data (from a DATA frame payload) has been received.
int OnDataChunk(nghttp2_session* session, uint8_t flags,
                Http2StreamId stream_id, const uint8_t* data, size_t len,
                void* user_data);

// Callback once a stream has been closed.
int OnStreamClosed(nghttp2_session* session, Http2StreamId stream_id,
                   uint32_t error_code, void* user_data);

// Invoked when nghttp2 has a chunk of extension frame data to pass to the
// application.
int OnExtensionChunkReceived(nghttp2_session* session,
                             const nghttp2_frame_hd* hd, const uint8_t* data,
                             size_t len, void* user_data);

// Invoked when nghttp2 wants the application to unpack an extension payload.
int OnUnpackExtensionCallback(nghttp2_session* session, void** payload,
                              const nghttp2_frame_hd* hd, void* user_data);

// Invoked when nghttp2 is ready to pack an extension payload. Returns the
// number of bytes serialized to |buf|.
ssize_t OnPackExtensionCallback(nghttp2_session* session, uint8_t* buf,
                                size_t len, const nghttp2_frame* frame,
                                void* user_data);

// Invoked when the library has an error message to deliver.
int OnError(nghttp2_session* session, int lib_error_code, const char* msg,
            size_t len, void* user_data);

nghttp2_session_callbacks_unique_ptr Create(
    nghttp2_send_data_callback send_data_callback);

}  // namespace callbacks
}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_NGHTTP2_CALLBACKS_H_
