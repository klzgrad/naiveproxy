#ifndef QUICHE_HTTP2_ADAPTER_HEADER_VALIDATOR_BASE_H_
#define QUICHE_HTTP2_ADAPTER_HEADER_VALIDATOR_BASE_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace adapter {

enum class HeaderType : uint8_t {
  REQUEST,
  REQUEST_TRAILER,
  RESPONSE_100,
  RESPONSE,
  RESPONSE_TRAILER,
};

enum class ObsTextOption : uint8_t {
  kAllow,
  kDisallow,
};

class QUICHE_EXPORT HeaderValidatorBase {
 public:
  HeaderValidatorBase() = default;
  virtual ~HeaderValidatorBase() = default;

  virtual void StartHeaderBlock() {
    status_.clear();
    content_length_ = std::nullopt;
  }

  enum HeaderStatus {
    HEADER_OK,
    HEADER_SKIP,
    HEADER_FIELD_INVALID,
    HEADER_FIELD_TOO_LONG,
  };
  virtual HeaderStatus ValidateSingleHeader(absl::string_view key,
                                            absl::string_view value) = 0;

  // Should return true if validation was successful.
  virtual bool FinishHeaderBlock(HeaderType type) = 0;

  // For responses, returns the value of the ":status" header, if present.
  absl::string_view status_header() const { return status_; }

  std::optional<size_t> content_length() const { return content_length_; }

  void SetMaxFieldSize(uint32_t field_size) { max_field_size_ = field_size; }
  void SetObsTextOption(ObsTextOption option) { obs_text_option_ = option; }
  // Allows the "extended CONNECT" syntax described in RFC 8441.
  void SetAllowExtendedConnect() { allow_extended_connect_ = true; }
  void SetValidatePath() { validate_path_ = true; }
  void SetAllowFragmentInPath() { allow_fragment_in_path_ = true; }
  void SetAllowDifferentHostAndAuthority() {
    allow_different_host_and_authority_ = true;
  }
  // If set, allow uppercase characters in header names (except for
  // pseudo-headers), in violation with RFC 9113 and RFC 9114.
  // Default behavior is to enforce that header names are lowercase.
  void SetAllowUppercaseInHeaderNames() {
    allow_uppercase_in_header_names_ = true;
  }

 protected:
  std::string status_;
  std::optional<size_t> max_field_size_;
  std::optional<size_t> content_length_;
  ObsTextOption obs_text_option_ = ObsTextOption::kDisallow;
  bool allow_extended_connect_ = false;
  bool validate_path_ = false;
  bool allow_fragment_in_path_ = false;
  bool allow_different_host_and_authority_ = false;
  bool allow_uppercase_in_header_names_ = false;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_HEADER_VALIDATOR_BASE_H_
