// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUIC_FLAGS_IMPL_H_
#define QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUIC_FLAGS_IMPL_H_

#include <string>

#include "quiche/common/platform/api/quiche_export.h"

#define QUIC_PROTOCOL_FLAG(type, flag, ...) \
  QUICHE_EXPORT_PRIVATE extern type FLAGS_##flag;
#include "quiche/quic/core/quic_protocol_flags_list.h"
#undef QUIC_PROTOCOL_FLAG

#endif  // QUICHE_COMMON_PLATFORM_DEFAULT_QUICHE_PLATFORM_IMPL_QUIC_FLAGS_IMPL_H_
