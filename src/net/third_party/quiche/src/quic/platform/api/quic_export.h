// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_EXPORT_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_EXPORT_H_

#include "quiche_platform_impl/quiche_export_impl.h"

// QUIC_EXPORT is not meant to be used.
#define QUIC_EXPORT QUICHE_EXPORT_IMPL

// QUIC_EXPORT_PRIVATE is meant for QUIC functionality that is built in Chromium
// as part of //net, and not fully contained in headers.
#define QUIC_EXPORT_PRIVATE QUICHE_EXPORT_PRIVATE_IMPL

// QUIC_NO_EXPORT is meant for QUIC functionality that is either fully defined
// in a header, or is built in Chromium as part of tests or tools.
#define QUIC_NO_EXPORT QUICHE_NO_EXPORT_IMPL

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_EXPORT_H_
