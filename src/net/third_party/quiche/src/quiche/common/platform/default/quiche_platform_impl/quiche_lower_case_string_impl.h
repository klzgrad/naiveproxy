#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_LOWER_CASE_STRING_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_LOWER_CASE_STRING_IMPL_H_

#include <string>

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quiche {

class QUICHE_EXPORT QuicheLowerCaseStringImpl {
 public:
  QuicheLowerCaseStringImpl(absl::string_view str)
      : str_(absl::AsciiStrToLower(str)) {}

  const std::string& get() const { return str_; }

 private:
  std::string str_;
};

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_LOWER_CASE_STRING_IMPL_H_
