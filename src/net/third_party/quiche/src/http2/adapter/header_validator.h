#ifndef QUICHE_HTTP2_ADAPTER_HEADER_VALIDATOR_H_
#define QUICHE_HTTP2_ADAPTER_HEADER_VALIDATOR_H_

#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "common/platform/api/quiche_export.h"

namespace http2 {
namespace adapter {

enum class HeaderType : uint8_t {
  REQUEST,
  RESPONSE_100,
  RESPONSE,
  RESPONSE_TRAILER,
};

class QUICHE_EXPORT_PRIVATE HeaderValidator {
 public:
  HeaderValidator() {}

  void StartHeaderBlock();

  enum HeaderStatus {
    HEADER_OK,
    HEADER_NAME_EMPTY,
    HEADER_NAME_INVALID_CHAR,
    HEADER_VALUE_INVALID_CHAR,
    HEADER_VALUE_INVALID_STATUS,
  };
  HeaderStatus ValidateSingleHeader(absl::string_view key,
                                    absl::string_view value);

  // Returns true if all required pseudoheaders and no extra pseudoheaders are
  // present for the given header type.
  bool FinishHeaderBlock(HeaderType type);

  absl::string_view status_header() const { return status_; }

 private:
  std::vector<std::string> pseudo_headers_;
  std::string status_;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_HEADER_VALIDATOR_H_
