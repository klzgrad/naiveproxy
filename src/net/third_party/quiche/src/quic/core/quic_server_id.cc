// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/core/quic_server_id.h"

#include <string>
#include <tuple>

#include "net/third_party/quiche/src/quic/platform/api/quic_estimate_memory_usage.h"

namespace quic {

QuicServerId::QuicServerId() : QuicServerId("", 0, false) {}

QuicServerId::QuicServerId(const std::string& host, uint16_t port)
    : QuicServerId(host, port, false) {}

QuicServerId::QuicServerId(const std::string& host,
                           uint16_t port,
                           bool privacy_mode_enabled)
    : host_(host), port_(port), privacy_mode_enabled_(privacy_mode_enabled) {}

QuicServerId::~QuicServerId() {}

bool QuicServerId::operator<(const QuicServerId& other) const {
  return std::tie(port_, host_, privacy_mode_enabled_) <
         std::tie(other.port_, other.host_, other.privacy_mode_enabled_);
}

bool QuicServerId::operator==(const QuicServerId& other) const {
  return privacy_mode_enabled_ == other.privacy_mode_enabled_ &&
         host_ == other.host_ && port_ == other.port_;
}

size_t QuicServerId::EstimateMemoryUsage() const {
  return QuicEstimateMemoryUsage(host_);
}

}  // namespace quic
