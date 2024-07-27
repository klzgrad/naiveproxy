#include "quiche/http2/adapter/nghttp2_data_provider.h"

#include <memory>

#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/http2/adapter/nghttp2_util.h"

namespace http2 {
namespace adapter {
namespace callbacks {

ssize_t VisitorReadCallback(Http2VisitorInterface& visitor, int32_t stream_id,
                            size_t max_length, uint32_t* data_flags) {
  *data_flags |= NGHTTP2_DATA_FLAG_NO_COPY;
  auto [payload_length, end_data, end_stream] =
      visitor.OnReadyToSendDataForStream(stream_id, max_length);
  if (payload_length == 0 && !end_data) {
    return NGHTTP2_ERR_DEFERRED;
  } else if (payload_length == DataFrameSource::kError) {
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  }
  if (end_data) {
    *data_flags |= NGHTTP2_DATA_FLAG_EOF;
  }
  if (!end_stream) {
    *data_flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
  }
  return payload_length;
}

ssize_t DataFrameSourceReadCallback(DataFrameSource& source, size_t length,
                                    uint32_t* data_flags) {
  *data_flags |= NGHTTP2_DATA_FLAG_NO_COPY;
  auto [result_length, done] = source.SelectPayloadLength(length);
  if (result_length == 0 && !done) {
    return NGHTTP2_ERR_DEFERRED;
  } else if (result_length == DataFrameSource::kError) {
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  }
  if (done) {
    *data_flags |= NGHTTP2_DATA_FLAG_EOF;
  }
  if (!source.send_fin()) {
    *data_flags |= NGHTTP2_DATA_FLAG_NO_END_STREAM;
  }
  return result_length;
}

}  // namespace callbacks
}  // namespace adapter
}  // namespace http2
