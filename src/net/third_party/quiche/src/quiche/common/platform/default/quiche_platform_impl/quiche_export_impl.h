#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_EXPORT_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_EXPORT_IMPL_H_

#include "absl/base/attributes.h"

// These macros are documented in: quiche/quic/platform/api/quic_export.h

#if defined(_WIN32)
#define QUICHE_EXPORT_IMPL
#elif ABSL_HAVE_ATTRIBUTE(visibility)
#define QUICHE_EXPORT_IMPL __attribute__((visibility("default")))
#else
#define QUICHE_EXPORT_IMPL
#endif

#define QUICHE_NO_EXPORT_IMPL QUICHE_EXPORT_IMPL

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUICHE_EXPORT_IMPL_H_
