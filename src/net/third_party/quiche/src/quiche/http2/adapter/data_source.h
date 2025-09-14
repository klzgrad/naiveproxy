#ifndef QUICHE_HTTP2_ADAPTER_DATA_SOURCE_H_
#define QUICHE_HTTP2_ADAPTER_DATA_SOURCE_H_

#include <cstdint>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace adapter {

// TODO(birenroy): move the remaining constants.
class QUICHE_EXPORT DataFrameSource {
 public:
  enum : int64_t { kBlocked = 0, kError = -1 };
};

// Represents a source of metadata frames for transmission to the peer.
class QUICHE_EXPORT MetadataSource {
 public:
  virtual ~MetadataSource() {}

  // Returns the number of frames of at most |max_frame_size| required to
  // serialize the metadata for this source. Only required by the nghttp2
  // implementation.
  virtual size_t NumFrames(size_t max_frame_size) const = 0;

  // This method is called with a destination buffer and length. It should
  // return the number of payload bytes copied to |dest|, or a negative integer
  // to indicate an error, as well as a boolean indicating whether the metadata
  // has been completely copied.
  virtual std::pair<int64_t, bool> Pack(uint8_t* dest, size_t dest_len) = 0;

  // This method is called when transmission of the metadata for this source
  // fails in a non-recoverable way.
  virtual void OnFailure() = 0;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_DATA_SOURCE_H_
