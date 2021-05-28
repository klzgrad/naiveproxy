#include "http2/adapter/nghttp2_util.h"

#include <cstdint>

#include "absl/strings/string_view.h"
#include "http2/adapter/http2_protocol.h"
#include "third_party/nghttp2/src/lib/includes/nghttp2/nghttp2.h"
#include "common/platform/api/quiche_logging.h"

namespace http2 {
namespace adapter {

uint8_t* ToUint8Ptr(char* str) { return reinterpret_cast<uint8_t*>(str); }
uint8_t* ToUint8Ptr(const char* str) {
  return const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(str));
}

absl::string_view ToStringView(nghttp2_rcbuf* rc_buffer) {
  nghttp2_vec buffer = nghttp2_rcbuf_get_buf(rc_buffer);
  return absl::string_view(reinterpret_cast<const char*>(buffer.base),
                           buffer.len);
}

absl::string_view ToStringView(uint8_t* pointer, size_t length) {
  return absl::string_view(reinterpret_cast<const char*>(pointer), length);
}

std::vector<nghttp2_nv> GetRequestNghttp2Nvs(absl::Span<const Header> headers) {
  const int num_headers = headers.size();
  auto nghttp2_nvs = std::vector<nghttp2_nv>(num_headers);
  for (int i = 0; i < num_headers; ++i) {
    nghttp2_nv header;
    header.name = ToUint8Ptr(&headers[i].first[0]);
    header.namelen = headers[i].first.size();
    header.value = ToUint8Ptr(&headers[i].second[0]);
    header.valuelen = headers[i].second.size();
    header.flags = NGHTTP2_FLAG_NONE;
    nghttp2_nvs.push_back(std::move(header));
  }

  return nghttp2_nvs;
}

std::vector<nghttp2_nv> GetResponseNghttp2Nvs(
    const spdy::Http2HeaderBlock& headers,
    absl::string_view response_code) {
  // Allocate enough for all headers and also the :status pseudoheader.
  const int num_headers = headers.size();
  auto nghttp2_nvs = std::vector<nghttp2_nv>(num_headers + 1);

  // Add the :status pseudoheader first.
  nghttp2_nv status;
  status.name = ToUint8Ptr(kHttp2StatusPseudoHeader);
  status.namelen = strlen(kHttp2StatusPseudoHeader);
  status.value = ToUint8Ptr(response_code.data());
  status.valuelen = response_code.size();
  status.flags = NGHTTP2_FLAG_NONE;
  nghttp2_nvs.push_back(std::move(status));

  // Add the remaining headers.
  for (const auto header_pair : headers) {
    nghttp2_nv header;
    header.name = ToUint8Ptr(header_pair.first.data());
    header.namelen = header_pair.first.size();
    header.value = ToUint8Ptr(header_pair.second.data());
    header.valuelen = header_pair.second.size();
    header.flags = NGHTTP2_FLAG_NONE;
    nghttp2_nvs.push_back(std::move(header));
  }

  return nghttp2_nvs;
}

Http2ErrorCode ToHttp2ErrorCode(uint32_t wire_error_code) {
  if (wire_error_code > static_cast<int>(Http2ErrorCode::MAX_ERROR_CODE)) {
    return Http2ErrorCode::INTERNAL_ERROR;
  }
  return static_cast<Http2ErrorCode>(wire_error_code);
}

}  // namespace adapter
}  // namespace http2
