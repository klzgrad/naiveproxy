#ifndef QUICHE_HTTP2_ADAPTER_HEADER_VALIDATOR_H_
#define QUICHE_HTTP2_ADAPTER_HEADER_VALIDATOR_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
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

class QUICHE_EXPORT_PRIVATE HeaderValidator {
 public:
  HeaderValidator() {}

  void SetMaxFieldSize(uint32_t field_size) { max_field_size_ = field_size; }

  // If called, this validator will allow the `:protocol` pseudo-header, as
  // described in RFC 8441.
  void AllowConnect() { allow_connect_ = true; }

  void StartHeaderBlock();

  enum HeaderStatus {
    HEADER_OK,
    HEADER_FIELD_INVALID,
    HEADER_FIELD_TOO_LONG,
  };
  HeaderStatus ValidateSingleHeader(absl::string_view key,
                                    absl::string_view value);

  // Returns true if all required pseudoheaders and no extra pseudoheaders are
  // present for the given header type.
  bool FinishHeaderBlock(HeaderType type);

  // For responses, returns the value of the ":status" header, if present.
  absl::string_view status_header() const { return status_; }

  absl::optional<size_t> content_length() const { return content_length_; }

 private:
  bool HandleContentLength(absl::string_view value);
  bool ValidateAndSetAuthority(absl::string_view authority);

  std::vector<std::string> pseudo_headers_;
  absl::optional<std::string> authority_ = absl::nullopt;
  std::string status_;
  std::string method_;
  std::string path_;
  absl::optional<size_t> max_field_size_;
  absl::optional<size_t> content_length_;
  bool allow_connect_ = false;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_HEADER_VALIDATOR_H_
