#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUIC_TESTVALUE_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUIC_TESTVALUE_IMPL_H_

#include "absl/strings/string_view.h"

namespace quic {

template <class T>
void AdjustTestValueImpl(absl::string_view /*label*/, T* /*var*/) {}

}  // namespace quic

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUIC_TESTVALUE_IMPL_H_
