// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_COMMON_PLATFORM_API_QUICHE_EXPORT_H_
#define QUICHE_COMMON_PLATFORM_API_QUICHE_EXPORT_H_

#include "quiche_platform_impl/quiche_export_impl.h"

// QUICHE_EXPORT is meant for QUICHE functionality that is built in
// Chromium as part of //net/third_party/quiche component, and not fully
// contained in headers.  It is required for Windows DLL builds to work.
#define QUICHE_EXPORT QUICHE_EXPORT_IMPL

// QUICHE_NO_EXPORT is meant for QUICHE functionality that is either fully
// defined in a header, or is built in Chromium as part of tests or tools.
#define QUICHE_NO_EXPORT QUICHE_NO_EXPORT_IMPL

#endif  // QUICHE_COMMON_PLATFORM_API_QUICHE_EXPORT_H_
