#ifndef QUICHE_HTTP2_ADAPTER_HEADER_VALIDATOR_H_
#define QUICHE_HTTP2_ADAPTER_HEADER_VALIDATOR_H_

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

  // Returns whether `value` is valid according to RFC 9110 Section 5.5 and RFC
  // 9112 Section 8.2.1.
  static bool IsValidHeaderValue(absl::string_view value,
                                 ObsTextOption ops_text_option);

  // Returns whether `authority` is valid according to RFC 3986 Section 3.2.
  static bool IsValidAuthority(absl::string_view authority);

 private:
  enum ContentLengthStatus {
    CONTENT_LENGTH_OK,
    CONTENT_LENGTH_SKIP,  // Used to handle duplicate content length values.
    CONTENT_LENGTH_ERROR,
  };
  ContentLengthStatus HandleContentLength(absl::string_view value);
  bool ValidateAndSetAuthority(absl::string_view authority);

  std::vector<std::string> pseudo_headers_;
  absl::optional<std::string> authority_ = absl::nullopt;
  std::string method_;
  std::string path_;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_HEADER_VALIDATOR_H_
