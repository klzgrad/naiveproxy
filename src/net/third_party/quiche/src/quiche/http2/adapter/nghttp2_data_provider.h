#ifndef QUICHE_HTTP2_ADAPTER_NGHTTP2_DATA_PROVIDER_H_
#define QUICHE_HTTP2_ADAPTER_NGHTTP2_DATA_PROVIDER_H_

#include <cstdint>
#include <memory>

#include "quiche/http2/adapter/data_source.h"
#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/http2/adapter/nghttp2.h"

namespace http2 {
namespace adapter {
namespace callbacks {

// A callback that returns DATA frame payload size and associated flags, given a
// Http2VisitorInterface.
ssize_t VisitorReadCallback(Http2VisitorInterface& visitor, int32_t stream_id,
                            size_t max_length, uint32_t* data_flags);

// A callback that returns DATA frame payload size and associated flags, given a
// DataFrameSource.
ssize_t DataFrameSourceReadCallback(DataFrameSource& source, size_t length,
                                    uint32_t* data_flags);

}  // namespace callbacks
}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_NGHTTP2_DATA_PROVIDER_H_
