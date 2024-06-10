#ifndef QUICHE_HTTP2_ADAPTER_HTTP2_PROTOCOL_H_
#define QUICHE_HTTP2_ADAPTER_HTTP2_PROTOCOL_H_

#include <cstdint>
#include <string>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/strings/string_view.h"
#include "absl/types/variant.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace adapter {

// Represents an HTTP/2 stream ID, consistent with nghttp2.
using Http2StreamId = int32_t;

// Represents an HTTP/2 SETTINGS parameter as specified in RFC 7540 Section 6.5.
using Http2SettingsId = uint16_t;

// Represents the payload of an HTTP/2 PING frame.
using Http2PingId = uint64_t;

// Represents a single header name or value.
using HeaderRep = absl::variant<absl::string_view, std::string>;

// Boolean return value is true if |rep| holds a string_view, which is assumed
// to have an indefinite lifetime.
std::pair<absl::string_view, bool> GetStringView(const HeaderRep& rep);

// Represents an HTTP/2 header field. A header field is a key-value pair with
// lowercase keys (as specified in RFC 7540 Section 8.1.2).
using Header = std::pair<HeaderRep, HeaderRep>;

// Represents an HTTP/2 SETTINGS key-value parameter.
struct QUICHE_EXPORT Http2Setting {
  Http2SettingsId id;
  uint32_t value;
};

QUICHE_EXPORT bool operator==(const Http2Setting& a, const Http2Setting& b);

// The maximum possible stream ID.
const Http2StreamId kMaxStreamId = 0x7FFFFFFF;

// The stream ID that represents the connection (e.g., for connection-level flow
// control updates).
const Http2StreamId kConnectionStreamId = 0;

// The default value for the size of the largest frame payload, according to RFC
// 7540 Section 6.5.2 (SETTINGS_MAX_FRAME_SIZE).
const uint32_t kDefaultFramePayloadSizeLimit = 16u * 1024u;

// The maximum value for the size of the largest frame payload, according to RFC
// 7540 Section 6.5.2 (SETTINGS_MAX_FRAME_SIZE).
const uint32_t kMaximumFramePayloadSizeLimit = 16777215u;

// The default value for the initial stream and connection flow control window
// size, according to RFC 7540 Section 6.9.2.
const int kInitialFlowControlWindowSize = 64 * 1024 - 1;

// The pseudo-header fields as specified in RFC 7540 Section 8.1.2.3 (request)
// and Section 8.1.2.4 (response).
ABSL_CONST_INIT QUICHE_EXPORT extern const char kHttp2MethodPseudoHeader[];
ABSL_CONST_INIT QUICHE_EXPORT extern const char kHttp2SchemePseudoHeader[];
ABSL_CONST_INIT QUICHE_EXPORT extern const char kHttp2AuthorityPseudoHeader[];
ABSL_CONST_INIT QUICHE_EXPORT extern const char kHttp2PathPseudoHeader[];
ABSL_CONST_INIT QUICHE_EXPORT extern const char kHttp2StatusPseudoHeader[];

ABSL_CONST_INIT QUICHE_EXPORT extern const uint8_t kMetadataFrameType;
ABSL_CONST_INIT QUICHE_EXPORT extern const uint8_t kMetadataEndFlag;
ABSL_CONST_INIT QUICHE_EXPORT extern const uint16_t kMetadataExtensionId;

enum class FrameType : uint8_t {
  DATA = 0x0,
  HEADERS,
  PRIORITY,
  RST_STREAM,
  SETTINGS,
  PUSH_PROMISE,
  PING,
  GOAWAY,
  WINDOW_UPDATE,
  CONTINUATION,
};

enum FrameFlags : uint8_t {
  END_STREAM_FLAG = 0x1,
  ACK_FLAG = END_STREAM_FLAG,
  END_HEADERS_FLAG = 0x4,
  PADDED_FLAG = 0x8,
  PRIORITY_FLAG = 0x20,
};

// HTTP/2 error codes as specified in RFC 7540 Section 7.
enum class Http2ErrorCode {
  HTTP2_NO_ERROR = 0x0,
  PROTOCOL_ERROR = 0x1,
  INTERNAL_ERROR = 0x2,
  FLOW_CONTROL_ERROR = 0x3,
  SETTINGS_TIMEOUT = 0x4,
  STREAM_CLOSED = 0x5,
  FRAME_SIZE_ERROR = 0x6,
  REFUSED_STREAM = 0x7,
  CANCEL = 0x8,
  COMPRESSION_ERROR = 0x9,
  CONNECT_ERROR = 0xA,
  ENHANCE_YOUR_CALM = 0xB,
  INADEQUATE_SECURITY = 0xC,
  HTTP_1_1_REQUIRED = 0xD,
  MAX_ERROR_CODE = HTTP_1_1_REQUIRED,
};

// The SETTINGS parameters defined in RFC 7540 Section 6.5.2. Endpoints may send
// SETTINGS parameters outside of these definitions as per RFC 7540 Section 5.5.
// This is explicitly an enum instead of an enum class for ease of implicit
// conversion to the underlying Http2SettingsId type and use with non-standard
// extension SETTINGS parameters.
enum Http2KnownSettingsId : Http2SettingsId {
  HEADER_TABLE_SIZE = 0x1,
  MIN_SETTING = HEADER_TABLE_SIZE,
  ENABLE_PUSH = 0x2,
  MAX_CONCURRENT_STREAMS = 0x3,
  INITIAL_WINDOW_SIZE = 0x4,
  MAX_FRAME_SIZE = 0x5,
  MAX_HEADER_LIST_SIZE = 0x6,
  ENABLE_CONNECT_PROTOCOL = 0x8,  // See RFC 8441
  MAX_SETTING = ENABLE_CONNECT_PROTOCOL
};

// Returns a human-readable string representation of the given SETTINGS |id| for
// logging/debugging. Returns "SETTINGS_UNKNOWN" for IDs outside of the RFC 7540
// Section 6.5.2 definitions.
QUICHE_EXPORT absl::string_view Http2SettingsIdToString(uint16_t id);

// Returns a human-readable string representation of the given |error_code| for
// logging/debugging. Returns "UNKNOWN_ERROR" for errors outside of RFC 7540
// Section 7 definitions.
QUICHE_EXPORT absl::string_view Http2ErrorCodeToString(
    Http2ErrorCode error_code);

enum class Perspective {
  kClient,
  kServer,
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_HTTP2_PROTOCOL_H_
