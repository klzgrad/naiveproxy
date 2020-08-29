// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_QUICHE_PLATFORM_API_QUICHE_EXPORT_H_
#define THIRD_PARTY_QUICHE_PLATFORM_API_QUICHE_EXPORT_H_

#include "net/quiche/common/platform/impl/quiche_export_impl.h"

// quiche_export_impl.h defines the following macros:
// - QUICHE_EXPORT is not meant to be used.
// - QUICHE_EXPORT_PRIVATE is meant for QUICHE functionality that is built in
//   Chromium as part of //net, and not fully contained in headers.
// - QUICHE_NO_EXPORT is meant for QUICHE functionality that is either fully
//   defined in a header, or is built in Chromium as part of tests or tools.

#endif  // THIRD_PARTY_QUICHE_PLATFORM_API_QUICHE_EXPORT_H_
