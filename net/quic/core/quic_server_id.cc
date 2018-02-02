// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/core/quic_server_id.h"

#include <tuple>

#include "net/quic/platform/api/quic_estimate_memory_usage.h"
#include "net/quic/platform/api/quic_str_cat.h"

using std::string;

namespace net {

QuicServerId::QuicServerId() : privacy_mode_(PRIVACY_MODE_DISABLED) {}

QuicServerId::QuicServerId(const HostPortPair& host_port_pair,
                           PrivacyMode privacy_mode)
    : host_port_pair_(host_port_pair), privacy_mode_(privacy_mode) {}

QuicServerId::QuicServerId(const string& host, uint16_t port)
    : host_port_pair_(host, port), privacy_mode_(PRIVACY_MODE_DISABLED) {}

QuicServerId::QuicServerId(const string& host,
                           uint16_t port,
                           PrivacyMode privacy_mode)
    : host_port_pair_(host, port), privacy_mode_(privacy_mode) {}

QuicServerId::~QuicServerId() {}

bool QuicServerId::operator<(const QuicServerId& other) const {
  return std::tie(host_port_pair_, privacy_mode_) <
         std::tie(other.host_port_pair_, other.privacy_mode_);
}

bool QuicServerId::operator==(const QuicServerId& other) const {
  return privacy_mode_ == other.privacy_mode_ &&
         host_port_pair_.Equals(other.host_port_pair_);
}

string QuicServerId::ToString() const {
  return QuicStrCat("https://", host_port_pair_.ToString(),
                    (privacy_mode_ == PRIVACY_MODE_ENABLED ? "/private" : ""));
}

size_t QuicServerId::EstimateMemoryUsage() const {
  return QuicEstimateMemoryUsage(host_port_pair_);
}

}  // namespace net
