#ifndef QUICHE_HTTP2_ADAPTER_NOOP_HEADER_VALIDATOR_H_
#define QUICHE_HTTP2_ADAPTER_NOOP_HEADER_VALIDATOR_H_

#include "absl/strings/string_view.h"
#include "quiche/http2/adapter/header_validator_base.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace http2 {
namespace adapter {

// A validator that does not actually perform any validation.
class QUICHE_EXPORT NoopHeaderValidator : public HeaderValidatorBase {
 public:
  NoopHeaderValidator() = default;

  HeaderStatus ValidateSingleHeader(absl::string_view key,
                                    absl::string_view value) override;

  bool FinishHeaderBlock(HeaderType type) override;
};

}  // namespace adapter
}  // namespace http2

#endif  // QUICHE_HTTP2_ADAPTER_NOOP_HEADER_VALIDATOR_H_
