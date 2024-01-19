#include "quiche/http2/adapter/header_validator.h"

#include <array>
#include <bitset>

#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "quiche/http2/adapter/header_validator_base.h"
#include "quiche/http2/http2_constants.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {
namespace adapter {

namespace {

// From RFC 9110 Section 5.6.2.
const absl::string_view kHttpTokenChars =
    "!#$%&'*+-.^_`|~0123456789"
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

const absl::string_view kHttp2HeaderNameAllowedChars =
    "!#$%&'*+-.0123456789"
    "^_`abcdefghijklmnopqrstuvwxyz|~";

const absl::string_view kHttp2HeaderValueAllowedChars =
    "\t "
    "!\"#$%&'()*+,-./"
    "0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`"
    "abcdefghijklmnopqrstuvwxyz{|}~";

const absl::string_view kHttp2StatusValueAllowedChars = "0123456789";

const absl::string_view kValidAuthorityChars =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~%!$&'()["
    "]*+,;=:";

const absl::string_view kValidPathChars =
    "/abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~%!$&'()"
    "*+,;=:@?";

const absl::string_view kValidPathCharsWithFragment =
    "/abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~%!$&'()"
    "*+,;=:@?#";

using CharMap = std::array<bool, 256>;

CharMap BuildValidCharMap(absl::string_view valid_chars) {
  CharMap map;
  map.fill(false);
  for (char c : valid_chars) {
    // Cast to uint8_t, guaranteed to have 8 bits. A char may have more, leading
    // to possible indices above 256.
    map[static_cast<uint8_t>(c)] = true;
  }
  return map;
}
CharMap AllowObsText(CharMap map) {
  // Characters above 0x80 are allowed in header field values as `obs-text` in
  // RFC 7230.
  for (uint8_t c = 0xff; c >= 0x80; --c) {
    map[c] = true;
  }
  return map;
}

bool AllCharsInMap(absl::string_view str, const CharMap& map) {
  for (char c : str) {
    if (!map[static_cast<uint8_t>(c)]) {
      return false;
    }
  }
  return true;
}

bool IsValidStatus(absl::string_view status) {
  static const CharMap valid_chars =
      BuildValidCharMap(kHttp2StatusValueAllowedChars);
  return AllCharsInMap(status, valid_chars);
}

bool IsValidMethod(absl::string_view method) {
  static const CharMap valid_chars = BuildValidCharMap(kHttpTokenChars);
  return AllCharsInMap(method, valid_chars);
}

}  // namespace

void HeaderValidator::StartHeaderBlock() {
  HeaderValidatorBase::StartHeaderBlock();
  pseudo_headers_.reset();
  pseudo_header_state_.reset();
  authority_.clear();
}

void HeaderValidator::RecordPseudoHeader(PseudoHeaderTag tag) {
  if (pseudo_headers_[tag]) {
    pseudo_headers_[TAG_UNKNOWN_EXTRA] = true;
  } else {
    pseudo_headers_[tag] = true;
  }
}

HeaderValidator::HeaderStatus HeaderValidator::ValidateSingleHeader(
    absl::string_view key, absl::string_view value) {
  if (key.empty()) {
    return HEADER_FIELD_INVALID;
  }
  if (max_field_size_.has_value() &&
      key.size() + value.size() > *max_field_size_) {
    QUICHE_VLOG(2) << "Header field size is " << key.size() + value.size()
                   << ", exceeds max size of " << *max_field_size_;
    return HEADER_FIELD_TOO_LONG;
  }
  if (key[0] == ':') {
    // Remove leading ':'.
    key.remove_prefix(1);
    if (key == "status") {
      if (value.size() != 3 || !IsValidStatus(value)) {
        QUICHE_VLOG(2) << "malformed status value: [" << absl::CEscape(value)
                       << "]";
        return HEADER_FIELD_INVALID;
      }
      if (value == "101") {
        // Switching protocols is not allowed on a HTTP/2 stream.
        return HEADER_FIELD_INVALID;
      }
      status_ = std::string(value);
      RecordPseudoHeader(TAG_STATUS);
    } else if (key == "method") {
      if (value == "OPTIONS") {
        pseudo_header_state_[STATE_METHOD_IS_OPTIONS] = true;
      } else if (value == "CONNECT") {
        pseudo_header_state_[STATE_METHOD_IS_CONNECT] = true;
      } else if (!IsValidMethod(value)) {
        return HEADER_FIELD_INVALID;
      }
      RecordPseudoHeader(TAG_METHOD);
    } else if (key == "authority") {
      if (!ValidateAndSetAuthority(value)) {
        return HEADER_FIELD_INVALID;
      }
      RecordPseudoHeader(TAG_AUTHORITY);
    } else if (key == "path") {
      if (value == "*") {
        pseudo_header_state_[STATE_PATH_IS_STAR] = true;
      } else if (value.empty()) {
        pseudo_header_state_[STATE_PATH_IS_EMPTY] = true;
        return HEADER_FIELD_INVALID;
      } else if (validate_path_ &&
                 !IsValidPath(value, allow_fragment_in_path_)) {
        return HEADER_FIELD_INVALID;
      }
      if (value[0] == '/') {
        pseudo_header_state_[STATE_PATH_INITIAL_SLASH] = true;
      }
      RecordPseudoHeader(TAG_PATH);
    } else if (key == "protocol") {
      RecordPseudoHeader(TAG_PROTOCOL);
    } else if (key == "scheme") {
      RecordPseudoHeader(TAG_SCHEME);
    } else {
      pseudo_headers_[TAG_UNKNOWN_EXTRA] = true;
      if (!IsValidHeaderName(key)) {
        QUICHE_VLOG(2) << "invalid chars in header name: ["
                       << absl::CEscape(key) << "]";
        return HEADER_FIELD_INVALID;
      }
    }
    if (!IsValidHeaderValue(value, obs_text_option_)) {
      QUICHE_VLOG(2) << "invalid chars in header value: ["
                     << absl::CEscape(value) << "]";
      return HEADER_FIELD_INVALID;
    }
  } else {
    std::string lowercase_key;
    if (allow_uppercase_in_header_names_) {
      // Convert header name to lowercase for validation and also for comparison
      // to lowercase string literals below.
      lowercase_key = absl::AsciiStrToLower(key);
      key = lowercase_key;
    }

    if (!IsValidHeaderName(key)) {
      QUICHE_VLOG(2) << "invalid chars in header name: [" << absl::CEscape(key)
                     << "]";
      return HEADER_FIELD_INVALID;
    }
    if (!IsValidHeaderValue(value, obs_text_option_)) {
      QUICHE_VLOG(2) << "invalid chars in header value: ["
                     << absl::CEscape(value) << "]";
      return HEADER_FIELD_INVALID;
    }
    if (key == "host") {
      if (pseudo_headers_[TAG_STATUS]) {
        // Response headers can contain "Host".
      } else {
        if (!ValidateAndSetAuthority(value)) {
          return HEADER_FIELD_INVALID;
        }
        pseudo_headers_[TAG_AUTHORITY] = true;
      }
    } else if (key == "content-length") {
      const ContentLengthStatus status = HandleContentLength(value);
      switch (status) {
        case CONTENT_LENGTH_ERROR:
          return HEADER_FIELD_INVALID;
        case CONTENT_LENGTH_SKIP:
          return HEADER_SKIP;
        case CONTENT_LENGTH_OK:
          return HEADER_OK;
        default:
          return HEADER_FIELD_INVALID;
      }
    } else if (key == "te" && value != "trailers") {
      return HEADER_FIELD_INVALID;
    } else if (key == "upgrade" || GetInvalidHttp2HeaderSet().contains(key)) {
      // TODO(b/78024822): Remove the "upgrade" here once it's added to
      // GetInvalidHttp2HeaderSet().
      return HEADER_FIELD_INVALID;
    }
  }
  return HEADER_OK;
}

// Returns true if all required pseudoheaders and no extra pseudoheaders are
// present for the given header type.
bool HeaderValidator::FinishHeaderBlock(HeaderType type) {
  switch (type) {
    case HeaderType::REQUEST:
      return ValidateRequestHeaders(pseudo_headers_, pseudo_header_state_,
                                    allow_extended_connect_);
    case HeaderType::REQUEST_TRAILER:
      return ValidateRequestTrailers(pseudo_headers_);
    case HeaderType::RESPONSE_100:
    case HeaderType::RESPONSE:
      return ValidateResponseHeaders(pseudo_headers_);
    case HeaderType::RESPONSE_TRAILER:
      return ValidateResponseTrailers(pseudo_headers_);
  }
  return false;
}

bool HeaderValidator::IsValidHeaderName(absl::string_view name) {
  static const CharMap valid_chars =
      BuildValidCharMap(kHttp2HeaderNameAllowedChars);
  return AllCharsInMap(name, valid_chars);
}

bool HeaderValidator::IsValidHeaderValue(absl::string_view value,
                                         ObsTextOption option) {
  static const CharMap valid_chars =
      BuildValidCharMap(kHttp2HeaderValueAllowedChars);
  static const CharMap valid_chars_with_obs_text =
      AllowObsText(BuildValidCharMap(kHttp2HeaderValueAllowedChars));
  return AllCharsInMap(value, option == ObsTextOption::kAllow
                                  ? valid_chars_with_obs_text
                                  : valid_chars);
}

bool HeaderValidator::IsValidAuthority(absl::string_view authority) {
  static const CharMap valid_chars = BuildValidCharMap(kValidAuthorityChars);
  return AllCharsInMap(authority, valid_chars);
}

bool HeaderValidator::IsValidPath(absl::string_view path, bool allow_fragment) {
  static const CharMap valid_chars = BuildValidCharMap(kValidPathChars);
  static const CharMap valid_chars_with_fragment =
      BuildValidCharMap(kValidPathCharsWithFragment);
  if (allow_fragment) {
    return AllCharsInMap(path, valid_chars_with_fragment);
  } else {
    return AllCharsInMap(path, valid_chars);
  }
}

HeaderValidator::ContentLengthStatus HeaderValidator::HandleContentLength(
    absl::string_view value) {
  if (value.empty()) {
    return CONTENT_LENGTH_ERROR;
  }

  if (status_ == "204" && value != "0") {
    // There should be no body in a "204 No Content" response.
    return CONTENT_LENGTH_ERROR;
  }
  if (!status_.empty() && status_[0] == '1' && value != "0") {
    // There should also be no body in a 1xx response.
    return CONTENT_LENGTH_ERROR;
  }

  size_t content_length = 0;
  const bool valid = absl::SimpleAtoi(value, &content_length);
  if (!valid) {
    return CONTENT_LENGTH_ERROR;
  }

  if (content_length_.has_value()) {
    return content_length == *content_length_ ? CONTENT_LENGTH_SKIP
                                              : CONTENT_LENGTH_ERROR;
  }
  content_length_ = content_length;
  return CONTENT_LENGTH_OK;
}

// Returns whether `authority` contains only characters from the `host` ABNF
// from RFC 3986 section 3.2.2.
bool HeaderValidator::ValidateAndSetAuthority(absl::string_view authority) {
  if (!IsValidAuthority(authority)) {
    return false;
  }
  if (!allow_different_host_and_authority_ && pseudo_headers_[TAG_AUTHORITY] &&
      authority != authority_) {
    return false;
  }
  if (!authority.empty()) {
    pseudo_header_state_[STATE_AUTHORITY_IS_NONEMPTY] = true;
    if (authority_.empty()) {
      authority_ = authority;
    } else {
      absl::StrAppend(&authority_, ", ", authority);
    }
  }
  return true;
}

bool HeaderValidator::ValidateRequestHeaders(
    const PseudoHeaderTagSet& pseudo_headers,
    const PseudoHeaderStateSet& pseudo_header_state,
    bool allow_extended_connect) {
  QUICHE_VLOG(2) << "Request pseudo-headers: [" << pseudo_headers
                 << "], pseudo_header_state: [" << pseudo_header_state
                 << "], allow_extended_connect: " << allow_extended_connect;
  if (pseudo_header_state[STATE_METHOD_IS_CONNECT]) {
    if (allow_extended_connect) {
      // See RFC 8441. Extended CONNECT should have: authority, method, path,
      // protocol and scheme pseudo-headers. The tags corresponding to status
      // and unknown_extra should not be set.
      static const auto* kExtendedConnectHeaders =
          new PseudoHeaderTagSet(0b0011111);
      if (pseudo_headers == *kExtendedConnectHeaders) {
        return true;
      }
    }
    // See RFC 7540 Section 8.3. Regular CONNECT should have authority and
    // method, but no other pseudo headers.
    static const auto* kConnectHeaders = new PseudoHeaderTagSet(0b0000011);
    return pseudo_header_state[STATE_AUTHORITY_IS_NONEMPTY] &&
           pseudo_headers == *kConnectHeaders;
  }

  if (pseudo_header_state[STATE_PATH_IS_EMPTY]) {
    return false;
  }
  if (pseudo_header_state[STATE_PATH_IS_STAR]) {
    if (!pseudo_header_state[STATE_METHOD_IS_OPTIONS]) {
      return false;
    }
  } else if (!pseudo_header_state[STATE_PATH_INITIAL_SLASH]) {
    return false;
  }

  // Regular HTTP requests require authority, method, path and scheme.
  static const auto* kRequiredHeaders = new PseudoHeaderTagSet(0b0010111);
  return pseudo_headers == *kRequiredHeaders;
}

bool HeaderValidator::ValidateRequestTrailers(
    const PseudoHeaderTagSet& pseudo_headers) {
  return pseudo_headers.none();
}

bool HeaderValidator::ValidateResponseHeaders(
    const PseudoHeaderTagSet& pseudo_headers) {
  // HTTP responses require only the status pseudo header.
  static const auto* kRequiredHeaders = new PseudoHeaderTagSet(0b0100000);
  return pseudo_headers == *kRequiredHeaders;
}

bool HeaderValidator::ValidateResponseTrailers(
    const PseudoHeaderTagSet& pseudo_headers) {
  return pseudo_headers.none();
}

}  // namespace adapter
}  // namespace http2
