// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_SERVER_ID_H_
#define QUICHE_QUIC_CORE_QUIC_SERVER_ID_H_

#include <cstdint>
#include <string>

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"

namespace quic {

// The id used to identify sessions. Includes the hostname, port, scheme and
// privacy_mode.
class QUIC_EXPORT_PRIVATE QuicServerId {
 public:
  QuicServerId();
  QuicServerId(const std::string& host, uint16_t port);
  QuicServerId(const std::string& host,
               uint16_t port,
               bool privacy_mode_enabled);
  ~QuicServerId();

  // Needed to be an element of std::set.
  bool operator<(const QuicServerId& other) const;
  bool operator==(const QuicServerId& other) const;

  const std::string& host() const { return host_; }

  uint16_t port() const { return port_; }

  bool privacy_mode_enabled() const { return privacy_mode_enabled_; }

  size_t EstimateMemoryUsage() const;

 private:
  std::string host_;
  uint16_t port_;
  bool privacy_mode_enabled_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_SERVER_ID_H_
