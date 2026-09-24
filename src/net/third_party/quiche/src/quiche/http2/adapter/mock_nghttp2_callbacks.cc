#include "quiche/http2/adapter/mock_nghttp2_callbacks.h"

#include "quiche/http2/adapter/nghttp2_util.h"

namespace http2 {
namespace adapter {
namespace test {

/* static */
nghttp2_session_callbacks_unique_ptr MockNghttp2Callbacks::GetCallbacks() {
  nghttp2_session_callbacks* callbacks;
  nghttp2_session_callbacks_new(&callbacks);

  // All of the callback implementations below just delegate to the mock methods
  // of |user_data|, which is assumed to be a MockNghttp2Callbacks*.
  nghttp2_session_callbacks_set_send_callback(
      callbacks,
      [](nghttp2_session*, const uint8_t* data, size_t length, int flags,
         void* user_data) -> ssize_t {
        return static_cast<MockNghttp2Callbacks*>(user_data)->Send(data, length,
                                                                   flags);
      });

  nghttp2_session_callbacks_set_send_data_callback(
      callbacks,
      [](nghttp2_session*, nghttp2_frame* frame, const uint8_t* framehd,
         size_t length, nghttp2_data_source* source, void* user_data) -> int {
        return static_cast<MockNghttp2Callbacks*>(user_data)->SendData(
            frame, framehd, length, source);
      });

  nghttp2_session_callbacks_set_on_begin_headers_callback(
      callbacks,
      [](nghttp2_session*, const nghttp2_frame* frame, void* user_data) -> int {
        return static_cast<MockNghttp2Callbacks*>(user_data)->OnBeginHeaders(
            frame);
      });

  nghttp2_session_callbacks_set_on_header_callback(
      callbacks,
      [](nghttp2_session*, const nghttp2_frame* frame, const uint8_t* raw_name,
         size_t name_length, const uint8_t* raw_value, size_t value_length,
         uint8_t flags, void* user_data) -> int {
        absl::string_view name = ToStringView(raw_name, name_length);
        absl::string_view value = ToStringView(raw_value, value_length);
        return static_cast<MockNghttp2Callbacks*>(user_data)->OnHeader(
            frame, name, value, flags);
      });

  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
      callbacks,
      [](nghttp2_session*, uint8_t flags, int32_t stream_id,
         const uint8_t* data, size_t len, void* user_data) -> int {
        absl::string_view chunk = ToStringView(data, len);
        return static_cast<MockNghttp2Callbacks*>(user_data)->OnDataChunkRecv(
            flags, stream_id, chunk);
      });

  nghttp2_session_callbacks_set_on_begin_frame_callback(
      callbacks,
      [](nghttp2_session*, const nghttp2_frame_hd* hd, void* user_data) -> int {
        return static_cast<MockNghttp2Callbacks*>(user_data)->OnBeginFrame(hd);
      });

  nghttp2_session_callbacks_set_on_frame_recv_callback(
      callbacks,
      [](nghttp2_session*, const nghttp2_frame* frame, void* user_data) -> int {
        return static_cast<MockNghttp2Callbacks*>(user_data)->OnFrameRecv(
            frame);
      });

  nghttp2_session_callbacks_set_on_stream_close_callback(
      callbacks,
      [](nghttp2_session*, int32_t stream_id, uint32_t error_code,
         void* user_data) -> int {
        return static_cast<MockNghttp2Callbacks*>(user_data)->OnStreamClose(
            stream_id, error_code);
      });

  nghttp2_session_callbacks_set_on_frame_send_callback(
      callbacks,
      [](nghttp2_session*, const nghttp2_frame* frame, void* user_data) -> int {
        return static_cast<MockNghttp2Callbacks*>(user_data)->OnFrameSend(
            frame);
      });

  nghttp2_session_callbacks_set_before_frame_send_callback(
      callbacks,
      [](nghttp2_session*, const nghttp2_frame* frame, void* user_data) -> int {
        return static_cast<MockNghttp2Callbacks*>(user_data)->BeforeFrameSend(
            frame);
      });

  nghttp2_session_callbacks_set_on_frame_not_send_callback(
      callbacks,
      [](nghttp2_session*, const nghttp2_frame* frame, int lib_error_code,
         void* user_data) -> int {
        return static_cast<MockNghttp2Callbacks*>(user_data)->OnFrameNotSend(
            frame, lib_error_code);
      });

  nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(
      callbacks,
      [](nghttp2_session*, const nghttp2_frame* frame, int error_code,
         void* user_data) -> int {
        return static_cast<MockNghttp2Callbacks*>(user_data)
            ->OnInvalidFrameRecv(frame, error_code);
      });

  nghttp2_session_callbacks_set_error_callback2(
      callbacks,
      [](nghttp2_session* /*session*/, int lib_error_code, const char* msg,
         size_t len, void* user_data) -> int {
        return static_cast<MockNghttp2Callbacks*>(user_data)->OnErrorCallback2(
            lib_error_code, msg, len);
      });

  nghttp2_session_callbacks_set_pack_extension_callback(
      callbacks,
      [](nghttp2_session*, uint8_t* buf, size_t len, const nghttp2_frame* frame,
         void* user_data) -> ssize_t {
        return static_cast<MockNghttp2Callbacks*>(user_data)->OnPackExtension(
            buf, len, frame);
      });
  return MakeCallbacksPtr(callbacks);
}

}  // namespace test
}  // namespace adapter
}  // namespace http2
