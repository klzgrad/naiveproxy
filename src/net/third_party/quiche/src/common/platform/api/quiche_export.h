// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_QUICHE_PLATFORM_API_QUICHE_EXPORT_H_
#define THIRD_PARTY_QUICHE_PLATFORM_API_QUICHE_EXPORT_H_

#include "quiche_platform_impl/quiche_export_impl.h"

// QUICHE_EXPORT is not meant to be used.
#define QUICHE_EXPORT QUICHE_EXPORT_IMPL

// QUICHE_EXPORT_PRIVATE is meant for QUICHE functionality that is built in
// Chromium as part of //net, and not fully contained in headers.
#define QUICHE_EXPORT_PRIVATE QUICHE_EXPORT_PRIVATE_IMPL

// QUICHE_NO_EXPORT is meant for QUICHE functionality that is either fully
// defined in a header, or is built in Chromium as part of tests or tools.
#define QUICHE_NO_EXPORT QUICHE_NO_EXPORT_IMPL

#endif  // THIRD_PARTY_QUICHE_PLATFORM_API_QUICHE_EXPORT_H_
