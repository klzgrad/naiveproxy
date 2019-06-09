// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_isolation_key.h"

namespace net {

NetworkIsolationKey::NetworkIsolationKey(
    const base::Optional<url::Origin>& top_frame_origin)
    : top_frame_origin_(top_frame_origin) {}

NetworkIsolationKey::NetworkIsolationKey() = default;

NetworkIsolationKey::NetworkIsolationKey(
    const NetworkIsolationKey& network_isolation_key) = default;

NetworkIsolationKey::~NetworkIsolationKey() = default;

NetworkIsolationKey& NetworkIsolationKey::operator=(
    const NetworkIsolationKey& network_isolation_key) = default;

NetworkIsolationKey& NetworkIsolationKey::operator=(
    NetworkIsolationKey&& network_isolation_key) = default;

std::string NetworkIsolationKey::ToString() const {
  if (top_frame_origin_ && !top_frame_origin_->opaque())
    return top_frame_origin_->Serialize();
  return std::string();
}

std::string NetworkIsolationKey::ToDebugString() const {
  if (!top_frame_origin_)
    return "null";
  return top_frame_origin_->GetDebugString();
}

bool NetworkIsolationKey::IsFullyPopulated() const {
  return top_frame_origin_.has_value();
}

bool NetworkIsolationKey::IsTransient() const {
  if (!IsFullyPopulated())
    return true;
  return top_frame_origin_->opaque();
}

}  // namespace net
