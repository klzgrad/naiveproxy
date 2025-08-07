#ifndef QUICHE_HTTP2_ADAPTER_OGHTTP2_UTIL_H_
#define QUICHE_HTTP2_ADAPTER_OGHTTP2_UTIL_H_

#include "absl/types/span.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace adapter {

QUICHE_EXPORT quiche::HttpHeaderBlock ToHeaderBlock(
    absl::Span<const Header> headers);

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_OGHTTP2_UTIL_H_
