#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_TESTVALUE_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_TESTVALUE_IMPL_H_

#include "absl/strings/string_view.h"

namespace quiche {

template <class T>
void AdjustTestValueImpl(absl::string_view /*label*/, T* /*var*/) {}

}  // namespace quiche

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_TESTVALUE_IMPL_H_
