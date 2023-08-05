// Various utility/conversion functions for compatibility with the nghttp2 API.

#ifndef QUICHE_HTTP2_ADAPTER_NGHTTP2_UTIL_H_
#define QUICHE_HTTP2_ADAPTER_NGHTTP2_UTIL_H_

#include <cstdint>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "quiche/http2/adapter/data_source.h"
#include "quiche/http2/adapter/http2_protocol.h"
#include "quiche/http2/adapter/http2_visitor_interface.h"
#include "quiche/http2/adapter/nghttp2.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace http2 {
namespace adapter {

// Return codes to represent various errors.
inline constexpr int kStreamCallbackFailureStatus =
    NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
inline constexpr int kCancelStatus = NGHTTP2_ERR_CANCEL;

using CallbacksDeleter = void (*)(nghttp2_session_callbacks*);
using SessionDeleter = void (*)(nghttp2_session*);

using nghttp2_session_callbacks_unique_ptr =
    std::unique_ptr<nghttp2_session_callbacks, CallbacksDeleter>;
using nghttp2_session_unique_ptr =
    std::unique_ptr<nghttp2_session, SessionDeleter>;

nghttp2_session_callbacks_unique_ptr MakeCallbacksPtr(
    nghttp2_session_callbacks* callbacks);
nghttp2_session_unique_ptr MakeSessionPtr(nghttp2_session* session);

uint8_t* ToUint8Ptr(char* str);
uint8_t* ToUint8Ptr(const char* str);

absl::string_view ToStringView(nghttp2_rcbuf* rc_buffer);
absl::string_view ToStringView(uint8_t* pointer, size_t length);
absl::string_view ToStringView(const uint8_t* pointer, size_t length);

// Returns the nghttp2 header structure from the given |headers|, which
// must have the correct pseudoheaders preceding other headers.
std::vector<nghttp2_nv> GetNghttp2Nvs(absl::Span<const Header> headers);

// Returns the nghttp2 header structure from the given response |headers|, with
// the :status pseudoheader first based on the given |response_code|. The
// |response_code| is passed in separately from |headers| for lifetime reasons.
std::vector<nghttp2_nv> GetResponseNghttp2Nvs(
    const spdy::Http2HeaderBlock& headers, absl::string_view response_code);

// Returns the HTTP/2 error code corresponding to the raw wire value, as defined
// in RFC 7540 Section 7. Unrecognized error codes are treated as INTERNAL_ERROR
// based on the RFC 7540 Section 7 suggestion.
Http2ErrorCode ToHttp2ErrorCode(uint32_t wire_error_code);

// Converts between the integer error code used by nghttp2 and the corresponding
// InvalidFrameError value.
int ToNgHttp2ErrorCode(Http2VisitorInterface::InvalidFrameError error);
Http2VisitorInterface::InvalidFrameError ToInvalidFrameError(int error);

// Transforms a nghttp2_data_provider into a DataFrameSource. Assumes that
// |provider| uses the zero-copy nghttp2_data_source_read_callback API. Unsafe
// otherwise.
std::unique_ptr<DataFrameSource> MakeZeroCopyDataFrameSource(
    nghttp2_data_provider provider, void* user_data,
    nghttp2_send_data_callback send_data);

void LogBeforeSend(const nghttp2_frame& frame);

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_NGHTTP2_UTIL_H_
