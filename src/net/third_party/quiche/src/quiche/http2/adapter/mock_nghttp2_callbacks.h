#ifndef QUICHE_HTTP2_ADAPTER_MOCK_NGHTTP2_CALLBACKS_H_
#define QUICHE_HTTP2_ADAPTER_MOCK_NGHTTP2_CALLBACKS_H_

#include <cstdint>

#include "absl/strings/string_view.h"
#include "quiche/http2/adapter/nghttp2.h"
#include "quiche/http2/adapter/nghttp2_util.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/common/platform/api/quiche_test.h"

namespace http2 {
namespace adapter {
namespace test {

// This class provides a set of mock nghttp2 callbacks for use in unit test
// expectations.
class QUICHE_NO_EXPORT MockNghttp2Callbacks {
 public:
  MockNghttp2Callbacks() = default;

  // The caller takes ownership of the |nghttp2_session_callbacks|.
  static nghttp2_session_callbacks_unique_ptr GetCallbacks();

  MOCK_METHOD(ssize_t, Send, (const uint8_t* data, size_t length, int flags),
              ());

  MOCK_METHOD(int, SendData,
              (nghttp2_frame * frame, const uint8_t* framehd, size_t length,
               nghttp2_data_source* source),
              ());

  MOCK_METHOD(int, OnBeginHeaders, (const nghttp2_frame* frame), ());

  MOCK_METHOD(int, OnHeader,
              (const nghttp2_frame* frame, absl::string_view name,
               absl::string_view value, uint8_t flags),
              ());

  MOCK_METHOD(int, OnDataChunkRecv,
              (uint8_t flags, int32_t stream_id, absl::string_view data), ());

  MOCK_METHOD(int, OnBeginFrame, (const nghttp2_frame_hd* hd), ());

  MOCK_METHOD(int, OnFrameRecv, (const nghttp2_frame* frame), ());

  MOCK_METHOD(int, OnStreamClose, (int32_t stream_id, uint32_t error_code), ());

  MOCK_METHOD(int, BeforeFrameSend, (const nghttp2_frame* frame), ());

  MOCK_METHOD(int, OnFrameSend, (const nghttp2_frame* frame), ());

  MOCK_METHOD(int, OnFrameNotSend,
              (const nghttp2_frame* frame, int lib_error_code), ());

  MOCK_METHOD(int, OnInvalidFrameRecv,
              (const nghttp2_frame* frame, int error_code), ());

  MOCK_METHOD(int, OnErrorCallback2,
              (int lib_error_code, const char* msg, size_t len), ());

  MOCK_METHOD(ssize_t, OnPackExtension,
              (uint8_t * buf, size_t len, const nghttp2_frame* frame), ());
};

}  // namespace test
}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_MOCK_NGHTTP2_CALLBACKS_H_
