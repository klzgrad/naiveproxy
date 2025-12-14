// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche_platform_impl/quiche_flags_impl.h"

#define QUICHE_FLAG(type, flag, internal_value, external_value, doc) \
  type FLAGS_##flag = external_value;
#include "quiche/common/quiche_feature_flags_list.h"
#undef QUICHE_FLAG

#define QUICHE_PROTOCOL_FLAG(type, flag, value, doc) type FLAGS_##flag = value;
#include "quiche/common/quiche_protocol_flags_list.h"
#undef QUICHE_PROTOCOL_FLAG
