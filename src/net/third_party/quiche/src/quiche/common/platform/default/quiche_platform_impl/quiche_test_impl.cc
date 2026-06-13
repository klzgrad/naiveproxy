// Copyright 2022 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche_platform_impl/quiche_test_impl.h"

#include "quiche/common/platform/api/quiche_flags.h"

QuicheFlagSaverImpl::QuicheFlagSaverImpl() {
#define QUICHE_FLAG(type, flag, internal_value, external_value, doc) \
  saved_##flag##_ = FLAGS_##flag;
#include "quiche/common/quiche_feature_flags_list.h"
#undef QUICHE_FLAG
#define QUICHE_PROTOCOL_FLAG(type, flag, ...) saved_##flag##_ = FLAGS_##flag;
#include "quiche/common/quiche_protocol_flags_list.h"
#undef QUICHE_PROTOCOL_FLAG
}

QuicheFlagSaverImpl::~QuicheFlagSaverImpl() {
#define QUICHE_FLAG(type, flag, internal_value, external_value, doc) \
  FLAGS_##flag = saved_##flag##_;
#include "quiche/common/quiche_feature_flags_list.h"  // NOLINT
#undef QUICHE_FLAG
#define QUICHE_PROTOCOL_FLAG(type, flag, ...) FLAGS_##flag = saved_##flag##_;
#include "quiche/common/quiche_protocol_flags_list.h"  // NOLINT
#undef QUICHE_PROTOCOL_FLAG
}
