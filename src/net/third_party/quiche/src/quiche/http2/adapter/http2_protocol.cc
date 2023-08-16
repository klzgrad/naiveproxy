#include "quiche/http2/adapter/http2_protocol.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace http2 {
namespace adapter {

ABSL_CONST_INIT const char kHttp2MethodPseudoHeader[] = ":method";
ABSL_CONST_INIT const char kHttp2SchemePseudoHeader[] = ":scheme";
ABSL_CONST_INIT const char kHttp2AuthorityPseudoHeader[] = ":authority";
ABSL_CONST_INIT const char kHttp2PathPseudoHeader[] = ":path";
ABSL_CONST_INIT const char kHttp2StatusPseudoHeader[] = ":status";

ABSL_CONST_INIT const uint8_t kMetadataFrameType = 0x4d;
ABSL_CONST_INIT const uint8_t kMetadataEndFlag = 0x04;
ABSL_CONST_INIT const uint16_t kMetadataExtensionId = 0x4d44;

std::pair<absl::string_view, bool> GetStringView(const HeaderRep& rep) {
  if (absl::holds_alternative<absl::string_view>(rep)) {
    return std::make_pair(absl::get<absl::string_view>(rep), true);
  } else {
    absl::string_view view = absl::get<std::string>(rep);
    return std::make_pair(view, false);
  }
}

bool operator==(const Http2Setting& a, const Http2Setting& b) {
  return a.id == b.id && a.value == b.value;
}

absl::string_view Http2SettingsIdToString(uint16_t id) {
  switch (id) {
    case Http2KnownSettingsId::HEADER_TABLE_SIZE:
      return "SETTINGS_HEADER_TABLE_SIZE";
    case Http2KnownSettingsId::ENABLE_PUSH:
      return "SETTINGS_ENABLE_PUSH";
    case Http2KnownSettingsId::MAX_CONCURRENT_STREAMS:
      return "SETTINGS_MAX_CONCURRENT_STREAMS";
    case Http2KnownSettingsId::INITIAL_WINDOW_SIZE:
      return "SETTINGS_INITIAL_WINDOW_SIZE";
    case Http2KnownSettingsId::MAX_FRAME_SIZE:
      return "SETTINGS_MAX_FRAME_SIZE";
    case Http2KnownSettingsId::MAX_HEADER_LIST_SIZE:
      return "SETTINGS_MAX_HEADER_LIST_SIZE";
  }
  return "SETTINGS_UNKNOWN";
}

absl::string_view Http2ErrorCodeToString(Http2ErrorCode error_code) {
  switch (error_code) {
    case Http2ErrorCode::HTTP2_NO_ERROR:
      return "HTTP2_NO_ERROR";
    case Http2ErrorCode::PROTOCOL_ERROR:
      return "PROTOCOL_ERROR";
    case Http2ErrorCode::INTERNAL_ERROR:
      return "INTERNAL_ERROR";
    case Http2ErrorCode::FLOW_CONTROL_ERROR:
      return "FLOW_CONTROL_ERROR";
    case Http2ErrorCode::SETTINGS_TIMEOUT:
      return "SETTINGS_TIMEOUT";
    case Http2ErrorCode::STREAM_CLOSED:
      return "STREAM_CLOSED";
    case Http2ErrorCode::FRAME_SIZE_ERROR:
      return "FRAME_SIZE_ERROR";
    case Http2ErrorCode::REFUSED_STREAM:
      return "REFUSED_STREAM";
    case Http2ErrorCode::CANCEL:
      return "CANCEL";
    case Http2ErrorCode::COMPRESSION_ERROR:
      return "COMPRESSION_ERROR";
    case Http2ErrorCode::CONNECT_ERROR:
      return "CONNECT_ERROR";
    case Http2ErrorCode::ENHANCE_YOUR_CALM:
      return "ENHANCE_YOUR_CALM";
    case Http2ErrorCode::INADEQUATE_SECURITY:
      return "INADEQUATE_SECURITY";
    case Http2ErrorCode::HTTP_1_1_REQUIRED:
      return "HTTP_1_1_REQUIRED";
  }
  return "UNKNOWN_ERROR";
}

}  // namespace adapter
}  // namespace http2
