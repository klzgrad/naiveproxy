#include "http2/adapter/header_validator.h"

#include "absl/strings/escaping.h"
#include "common/platform/api/quiche_logging.h"

namespace http2 {
namespace adapter {

namespace {

const absl::string_view kHttp2HeaderNameAllowedChars =
    "!#$%&\'*+-.0123456789"
    "^_`abcdefghijklmnopqrstuvwxyz|~";

const absl::string_view kHttp2HeaderValueAllowedChars =
    "\t "
    "!\"#$%&'()*+,-./"
    "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
    "abcdefghijklmnopqrstuvwxyz{|}~";

const absl::string_view kHttp2StatusValueAllowedChars = "0123456789";

// TODO(birenroy): Support websocket requests, which contain an extra
// `:protocol` pseudo-header.
bool ValidateRequestHeaders(const std::vector<std::string>& pseudo_headers) {
  static const std::vector<std::string>* kRequiredHeaders =
      new std::vector<std::string>(
          {":authority", ":method", ":path", ":scheme"});
  return pseudo_headers == *kRequiredHeaders;
}

bool ValidateResponseHeaders(const std::vector<std::string>& pseudo_headers) {
  static const std::vector<std::string>* kRequiredHeaders =
      new std::vector<std::string>({":status"});
  return pseudo_headers == *kRequiredHeaders;
}

bool ValidateResponseTrailers(const std::vector<std::string>& pseudo_headers) {
  return pseudo_headers.empty();
}

}  // namespace

void HeaderValidator::StartHeaderBlock() {
  pseudo_headers_.clear();
  status_.clear();
}

HeaderValidator::HeaderStatus HeaderValidator::ValidateSingleHeader(
    absl::string_view key, absl::string_view value) {
  if (key.empty()) {
    return HEADER_NAME_EMPTY;
  }
  const absl::string_view validated_key = key[0] == ':' ? key.substr(1) : key;
  if (validated_key.find_first_not_of(kHttp2HeaderNameAllowedChars) !=
      absl::string_view::npos) {
    QUICHE_VLOG(2) << "invalid chars in header name: ["
                   << absl::CEscape(validated_key) << "]";
    return HEADER_NAME_INVALID_CHAR;
  }
  if (value.find_first_not_of(kHttp2HeaderValueAllowedChars) !=
      absl::string_view::npos) {
    QUICHE_VLOG(2) << "invalid chars in header value: [" << absl::CEscape(value)
                   << "]";
    return HEADER_VALUE_INVALID_CHAR;
  }
  if (key[0] == ':') {
    if (key == ":status") {
      if (value.size() != 3 ||
          value.find_first_not_of(kHttp2StatusValueAllowedChars) !=
              absl::string_view::npos) {
        QUICHE_VLOG(2) << "malformed status value: [" << absl::CEscape(value)
                       << "]";
        return HEADER_VALUE_INVALID_CHAR;
      }
      if (value == "101") {
        // Switching protocols is not allowed on a HTTP/2 stream.
        return HEADER_VALUE_INVALID_STATUS;
      }
      status_ = std::string(value);
    }
    pseudo_headers_.push_back(std::string(key));
  }
  return HEADER_OK;
}

// Returns true if all required pseudoheaders and no extra pseudoheaders are
// present for the given header type.
bool HeaderValidator::FinishHeaderBlock(HeaderType type) {
  std::sort(pseudo_headers_.begin(), pseudo_headers_.end());
  switch (type) {
    case HeaderType::REQUEST:
      return ValidateRequestHeaders(pseudo_headers_);
    case HeaderType::RESPONSE_100:
    case HeaderType::RESPONSE:
      return ValidateResponseHeaders(pseudo_headers_);
    case HeaderType::RESPONSE_TRAILER:
      return ValidateResponseTrailers(pseudo_headers_);
  }
  return false;
}

}  // namespace adapter
}  // namespace http2
