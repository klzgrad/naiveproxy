#ifndef QUICHE_HTTP2_ADAPTER_HTTP2_UTIL_H_
#define QUICHE_HTTP2_ADAPTER_HTTP2_UTIL_H_

#include "http2/adapter/http2_protocol.h"
#include "common/platform/api/quiche_export.h"
#include "spdy/core/spdy_protocol.h"

namespace http2 {
namespace adapter {

QUICHE_EXPORT_PRIVATE spdy::SpdyErrorCode TranslateErrorCode(
    Http2ErrorCode code);
QUICHE_EXPORT_PRIVATE Http2ErrorCode
TranslateErrorCode(spdy::SpdyErrorCode code);

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_HTTP2_UTIL_H_
