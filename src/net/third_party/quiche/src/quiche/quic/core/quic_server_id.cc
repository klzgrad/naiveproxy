// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/core/quic_server_id.h"

#include <string>
#include <tuple>
#include <utility>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "quiche/common/platform/api/quiche_googleurl.h"
#include "quiche/common/platform/api/quiche_logging.h"

namespace quic {

// static
absl::optional<QuicServerId> QuicServerId::ParseFromHostPortString(
    absl::string_view host_port_string) {
  url::Component username_component;
  url::Component password_component;
  url::Component host_component;
  url::Component port_component;

  url::ParseAuthority(host_port_string.data(),
                      url::Component(0, host_port_string.size()),
                      &username_component, &password_component, &host_component,
                      &port_component);

  // Only support "host:port" and nothing more or less.
  if (username_component.is_valid() || password_component.is_valid() ||
      !host_component.is_nonempty() || !port_component.is_nonempty()) {
    QUICHE_DVLOG(1) << "QuicServerId could not be parsed: " << host_port_string;
    return absl::nullopt;
  }

  std::string hostname(host_port_string.data() + host_component.begin,
                       host_component.len);

  int parsed_port_number =
      url::ParsePort(host_port_string.data(), port_component);
  // Negative result is either invalid or unspecified, either of which is
  // disallowed for this parse. Port 0 is technically valid but reserved and not
  // really usable in practice, so easiest to just disallow it here.
  if (parsed_port_number <= 0) {
    QUICHE_DVLOG(1)
        << "Port could not be parsed while parsing QuicServerId from: "
        << host_port_string;
    return absl::nullopt;
  }
  QUICHE_DCHECK_LE(parsed_port_number, std::numeric_limits<uint16_t>::max());

  return QuicServerId(std::move(hostname),
                      static_cast<uint16_t>(parsed_port_number));
}

QuicServerId::QuicServerId() : QuicServerId("", 0, false) {}

QuicServerId::QuicServerId(std::string host, uint16_t port)
    : QuicServerId(std::move(host), port, false) {}

QuicServerId::QuicServerId(std::string host, uint16_t port,
                           bool privacy_mode_enabled)
    : host_(std::move(host)),
      port_(port),
      privacy_mode_enabled_(privacy_mode_enabled) {}

QuicServerId::~QuicServerId() {}

bool QuicServerId::operator<(const QuicServerId& other) const {
  return std::tie(port_, host_, privacy_mode_enabled_) <
         std::tie(other.port_, other.host_, other.privacy_mode_enabled_);
}

bool QuicServerId::operator==(const QuicServerId& other) const {
  return privacy_mode_enabled_ == other.privacy_mode_enabled_ &&
         host_ == other.host_ && port_ == other.port_;
}

bool QuicServerId::operator!=(const QuicServerId& other) const {
  return !(*this == other);
}

std::string QuicServerId::ToHostPortString() const {
  return absl::StrCat(GetHostWithIpv6Brackets(), ":", port_);
}

absl::string_view QuicServerId::GetHostWithoutIpv6Brackets() const {
  if (host_.length() > 2 && host_.front() == '[' && host_.back() == ']') {
    return absl::string_view(host_.data() + 1, host_.length() - 2);
  } else {
    return host_;
  }
}

std::string QuicServerId::GetHostWithIpv6Brackets() const {
  if (!absl::StrContains(host_, ':') || host_.length() <= 2 ||
      (host_.front() == '[' && host_.back() == ']')) {
    return host_;
  } else {
    return absl::StrCat("[", host_, "]");
  }
}

}  // namespace quic
