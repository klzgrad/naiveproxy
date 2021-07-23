#include "http2/adapter/nghttp2_util.h"

#include <cstdint>

#include "absl/strings/string_view.h"
#include "http2/adapter/http2_protocol.h"
#include "third_party/nghttp2/src/lib/includes/nghttp2/nghttp2.h"
#include "common/platform/api/quiche_logging.h"

namespace http2 {
namespace adapter {

namespace {

void DeleteCallbacks(nghttp2_session_callbacks* callbacks) {
  if (callbacks) {
    nghttp2_session_callbacks_del(callbacks);
  }
}

void DeleteSession(nghttp2_session* session) {
  if (session) {
    nghttp2_session_del(session);
  }
}

}  // namespace

nghttp2_session_callbacks_unique_ptr MakeCallbacksPtr(
    nghttp2_session_callbacks* callbacks) {
  return nghttp2_session_callbacks_unique_ptr(callbacks, DeleteCallbacks);
}

nghttp2_session_unique_ptr MakeSessionPtr(nghttp2_session* session) {
  return nghttp2_session_unique_ptr(session, DeleteSession);
}

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

absl::string_view ToStringView(const uint8_t* pointer, size_t length) {
  return absl::string_view(reinterpret_cast<const char*>(pointer), length);
}

std::vector<nghttp2_nv> GetNghttp2Nvs(absl::Span<const Header> headers) {
  const int num_headers = headers.size();
  auto nghttp2_nvs = std::vector<nghttp2_nv>(num_headers);
  for (int i = 0; i < num_headers; ++i) {
    nghttp2_nv header;
    uint8_t flags = NGHTTP2_NV_FLAG_NONE;

    const auto [name, no_copy_name] = GetStringView(headers[i].first);
    header.name = ToUint8Ptr(name.data());
    header.namelen = name.size();
    if (no_copy_name) {
      flags |= NGHTTP2_NV_FLAG_NO_COPY_NAME;
    }
    const auto [value, no_copy_value] = GetStringView(headers[i].second);
    header.value = ToUint8Ptr(value.data());
    header.valuelen = value.size();
    if (no_copy_value) {
      flags |= NGHTTP2_NV_FLAG_NO_COPY_VALUE;
    }
    header.flags = flags;
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
