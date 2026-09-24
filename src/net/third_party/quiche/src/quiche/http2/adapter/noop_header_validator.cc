#include "quiche/http2/adapter/noop_header_validator.h"

#include <string>

#include "absl/strings/escaping.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace http2 {
namespace adapter {

HeaderValidatorBase::HeaderStatus NoopHeaderValidator::ValidateSingleHeader(
    absl::string_view key, absl::string_view value) {
  if (key == ":status") {
    status_ = std::string(value);
  }
  return HEADER_OK;
}

bool NoopHeaderValidator::FinishHeaderBlock(HeaderType /* type */) {
  return true;
}

}  // namespace adapter
}  // namespace http2
