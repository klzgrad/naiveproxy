#include "quiche/http2/adapter/oghttp2_util.h"

namespace http2 {
namespace adapter {

quiche::HttpHeaderBlock ToHeaderBlock(absl::Span<const Header> headers) {
  quiche::HttpHeaderBlock block;
  for (const Header& header : headers) {
    absl::string_view name = GetStringView(header.first).first;
    absl::string_view value = GetStringView(header.second).first;
    block.AppendValueOrAddHeader(name, value);
  }
  return block;
}

}  // namespace adapter
}  // namespace http2
