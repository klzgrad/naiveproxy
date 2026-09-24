// Copyright 2023 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche_platform_impl/quiche_logging_impl.h"

#include "absl/flags/flag.h"
#include "absl/log/absl_log.h"
#include "absl/strings/string_view.h"

#ifndef ABSL_VLOG
ABSL_FLAG(int, v, 0, "Show all QUICHE_VLOG(m) messages for m <= this.");
#endif
