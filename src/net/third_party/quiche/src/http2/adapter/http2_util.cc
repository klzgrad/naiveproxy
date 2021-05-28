#include "http2/adapter/http2_util.h"

namespace http2 {
namespace adapter {

spdy::SpdyErrorCode TranslateErrorCode(Http2ErrorCode code) {
  switch (code) {
    case Http2ErrorCode::NO_ERROR:
      return spdy::ERROR_CODE_NO_ERROR;
    case Http2ErrorCode::PROTOCOL_ERROR:
      return spdy::ERROR_CODE_PROTOCOL_ERROR;
    case Http2ErrorCode::INTERNAL_ERROR:
      return spdy::ERROR_CODE_INTERNAL_ERROR;
    case Http2ErrorCode::FLOW_CONTROL_ERROR:
      return spdy::ERROR_CODE_FLOW_CONTROL_ERROR;
    case Http2ErrorCode::SETTINGS_TIMEOUT:
      return spdy::ERROR_CODE_SETTINGS_TIMEOUT;
    case Http2ErrorCode::STREAM_CLOSED:
      return spdy::ERROR_CODE_STREAM_CLOSED;
    case Http2ErrorCode::FRAME_SIZE_ERROR:
      return spdy::ERROR_CODE_FRAME_SIZE_ERROR;
    case Http2ErrorCode::REFUSED_STREAM:
      return spdy::ERROR_CODE_REFUSED_STREAM;
    case Http2ErrorCode::CANCEL:
      return spdy::ERROR_CODE_CANCEL;
    case Http2ErrorCode::COMPRESSION_ERROR:
      return spdy::ERROR_CODE_COMPRESSION_ERROR;
    case Http2ErrorCode::CONNECT_ERROR:
      return spdy::ERROR_CODE_CONNECT_ERROR;
    case Http2ErrorCode::ENHANCE_YOUR_CALM:
      return spdy::ERROR_CODE_ENHANCE_YOUR_CALM;
    case Http2ErrorCode::INADEQUATE_SECURITY:
      return spdy::ERROR_CODE_INADEQUATE_SECURITY;
    case Http2ErrorCode::HTTP_1_1_REQUIRED:
      return spdy::ERROR_CODE_HTTP_1_1_REQUIRED;
  }
}

}  // namespace adapter
}  // namespace http2
