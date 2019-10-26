// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_SSL_HANDSHAKE_DETAILS_H_
#define NET_SSL_SSL_HANDSHAKE_DETAILS_H_

namespace net {

// This enum is persisted into histograms. Values may not be renumbered.
enum class SSLHandshakeDetails {
  // TLS 1.2 (or earlier) full handshake (2-RTT)
  kTLS12Full = 0,
  // TLS 1.2 (or earlier) resumption (1-RTT)
  kTLS12Resume = 1,
  // TLS 1.2 full handshake with False Start (1-RTT)
  kTLS12FalseStart = 2,
  // TLS 1.3 full handshake (1-RTT, usually)
  kTLS13Full = 3,
  // TLS 1.3 resumption handshake (1-RTT, usually)
  kTLS13Resume = 4,
  // TLS 1.3 0-RTT handshake (0-RTT)
  kTLS13Early = 5,
  kMaxValue = kTLS13Early,
};

}  // namespace net

#endif  // NET_SSL_SSL_HANDSHAKE_DETAILS_H_
