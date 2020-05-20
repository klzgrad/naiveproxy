// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Some helpers for quic that are for chromium codebase.

#ifndef NET_QUIC_QUIC_UTILS_CHROMIUM_H_
#define NET_QUIC_QUIC_UTILS_CHROMIUM_H_

#include <string>

#include "base/logging.h"
#include "net/base/net_export.h"
#include "net/third_party/quiche/src/quic/core/quic_tag.h"
#include "net/third_party/quiche/src/quic/core/quic_versions.h"

namespace net {

// Returns the list of QUIC tags represented by the comma separated
// string in |connection_options|.
NET_EXPORT quic::QuicTagVector ParseQuicConnectionOptions(
    const std::string& connection_options);

}  // namespace net

#endif  // NET_QUIC_QUIC_UTILS_CHROMIUM_H_
