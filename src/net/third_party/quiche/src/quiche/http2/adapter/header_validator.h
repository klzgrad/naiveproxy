#ifndef QUICHE_HTTP2_ADAPTER_HEADER_VALIDATOR_H_
#define QUICHE_HTTP2_ADAPTER_HEADER_VALIDATOR_H_

#include <bitset>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "quiche/http2/adapter/header_validator_base.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace adapter {

class QUICHE_EXPORT HeaderValidator : public HeaderValidatorBase {
 public:
  HeaderValidator() = default;

  void StartHeaderBlock() override;

  HeaderStatus ValidateSingleHeader(absl::string_view key,
                                    absl::string_view value) override;

  // Returns true if all required pseudoheaders and no extra pseudoheaders are
  // present for the given header type.
  bool FinishHeaderBlock(HeaderType type) override;

  // Returns whether `name` is valid according to RFC 9110 Section 5.1.
  // ':' is an invalid character, therefore HTTP/2 pseudo-headers must be
  // validated with the leading colon removed.
  static bool IsValidHeaderName(absl::string_view name);

  // Returns whether `value` is valid according to RFC 9110 Section 5.5 and
  // RFC 9113 Section 8.2.1.
  static bool IsValidHeaderValue(absl::string_view value,
                                 ObsTextOption ops_text_option);

  // Returns whether `authority` is valid according to RFC 3986 Section 3.2.
  static bool IsValidAuthority(absl::string_view authority);

  // Returns whether `path` is valid according to RFC 3986 Section 3.3. May
  // contain the query part of a URI.
  static bool IsValidPath(absl::string_view path, bool allow_fragment);

 private:
  enum ContentLengthStatus {
    CONTENT_LENGTH_OK,
    CONTENT_LENGTH_SKIP,  // Used to handle duplicate content length values.
    CONTENT_LENGTH_ERROR,
  };
  ContentLengthStatus HandleContentLength(absl::string_view value);
  bool ValidateAndSetAuthority(absl::string_view authority);

  enum PseudoHeaderTag {
    TAG_AUTHORITY = 0,
    TAG_METHOD,
    TAG_PATH,
    TAG_PROTOCOL,
    TAG_SCHEME,
    TAG_STATUS,
    TAG_UNKNOWN_EXTRA,
    TAG_ENUM_SIZE,
  };
  void RecordPseudoHeader(PseudoHeaderTag tag);

  using PseudoHeaderTagSet = std::bitset<TAG_ENUM_SIZE>;

  enum PseudoHeaderState {
    STATE_AUTHORITY_IS_NONEMPTY,
    STATE_METHOD_IS_OPTIONS,
    STATE_METHOD_IS_CONNECT,
    STATE_PATH_IS_EMPTY,
    STATE_PATH_IS_STAR,
    STATE_PATH_INITIAL_SLASH,
    STATE_ENUM_SIZE,
  };
  using PseudoHeaderStateSet = std::bitset<STATE_ENUM_SIZE>;

  static bool ValidateRequestHeaders(
      const PseudoHeaderTagSet& pseudo_headers,
      const PseudoHeaderStateSet& pseudo_header_state,
      bool allow_extended_connect);
  static bool ValidateRequestTrailers(const PseudoHeaderTagSet& pseudo_headers);
  static bool ValidateResponseHeaders(const PseudoHeaderTagSet& pseudo_headers);
  static bool ValidateResponseTrailers(
      const PseudoHeaderTagSet& pseudo_headers);

  PseudoHeaderTagSet pseudo_headers_;
  PseudoHeaderStateSet pseudo_header_state_;

  std::string authority_;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_HEADER_VALIDATOR_H_
