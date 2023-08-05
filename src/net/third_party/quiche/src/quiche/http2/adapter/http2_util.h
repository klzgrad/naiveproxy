#ifndef QUICHE_HTTP2_ADAPTER_HTTP2_UTIL_H_
#define QUICHE_HTTP2_ADAPTER_HTTP2_UTIL_H_

#include <cstdint>

#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/common/platform/api/quiche_export.h"
#include "quiche/spdy/core/spdy_protocol.h"

namespace http2 {
namespace adapter {

QUICHE_EXPORT spdy::SpdyErrorCode TranslateErrorCode(Http2ErrorCode code);
QUICHE_EXPORT Http2ErrorCode TranslateErrorCode(spdy::SpdyErrorCode code);

QUICHE_EXPORT absl::string_view ConnectionErrorToString(
    Http2VisitorInterface::ConnectionError error);

QUICHE_EXPORT absl::string_view InvalidFrameErrorToString(
    Http2VisitorInterface::InvalidFrameError error);

// A WINDOW_UPDATE sending strategy that returns true if the `delta` to be sent
// is positive and at least half of the window `limit`.
QUICHE_EXPORT bool DeltaAtLeastHalfLimit(int64_t limit, int64_t /*size*/,
                                         int64_t delta);

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_HTTP2_UTIL_H_
