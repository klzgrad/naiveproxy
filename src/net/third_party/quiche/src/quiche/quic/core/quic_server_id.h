// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_SERVER_ID_H_
#define QUICHE_QUIC_CORE_QUIC_SERVER_ID_H_

#include <cstdint>
#include <optional>
#include <string>

#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"
#include "quiche/quic/platform/api/quic_export.h"

namespace quic {

// The id used to identify sessions. Includes the hostname, port, scheme and
// privacy_mode.
class QUICHE_EXPORT QuicServerId {
 public:
  // Attempts to parse a QuicServerId from a "host:port" string. Returns nullopt
  // if input could not be parsed. Requires input to contain both host and port
  // and no other components of a URL authority.
  static std::optional<QuicServerId> ParseFromHostPortString(
      absl::string_view host_port_string);

  QuicServerId();
  QuicServerId(std::string host, uint16_t port);
  QuicServerId(std::string host, uint16_t port, std::string cache_key);
  ~QuicServerId();

  // Needed to be an element of an ordered container.
  bool operator<(const QuicServerId& other) const;
  bool operator==(const QuicServerId& other) const;

  bool operator!=(const QuicServerId& other) const;

  const std::string& host() const { return host_; }

  uint16_t port() const { return port_; }

  // This is the key used by SessionCache to retrieve the cached session.
  const std::string& cache_key() const { return cache_key_; }

  // Returns a "host:port" representation. IPv6 literal hosts will always be
  // bracketed in result.
  std::string ToHostPortString() const;

  // If host is an IPv6 literal surrounded by [], returns the substring without
  // []. Otherwise, returns host as is.
  absl::string_view GetHostWithoutIpv6Brackets() const;

  // If host is an IPv6 literal without surrounding [], returns host wrapped in
  // []. Otherwise, returns host as is.
  std::string GetHostWithIpv6Brackets() const;

  template <typename H>
  friend H AbslHashValue(H h, const QuicServerId& server_id) {
    return H::combine(std::move(h), server_id.host_, server_id.port_,
                      server_id.cache_key_);
  }

 private:
  std::string host_;
  uint16_t port_;

  // Key used for order comparison, equality and hashing.
  std::string cache_key_;
};

using QuicServerIdHash = absl::Hash<QuicServerId>;

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_SERVER_ID_H_
