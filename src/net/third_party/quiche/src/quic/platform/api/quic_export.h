// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_EXPORT_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_EXPORT_H_

#include "net/quic/platform/impl/quic_export_impl.h"

// quic_export_impl.h defines the following macros:
// - QUIC_EXPORT is not meant to be used.
// - QUIC_EXPORT_PRIVATE is meant for QUIC functionality that is built in
//   Chromium as part of //net, and not fully contained in headers.
// - QUIC_NO_EXPORT is meant for QUIC functionality that is either fully defined
//   in a header, or is built in Chromium as part of tests or tools.

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_EXPORT_H_
