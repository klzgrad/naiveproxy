// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_isolation_key.h"
#include "base/feature_list.h"
#include "net/base/features.h"

namespace net {

namespace {

std::string GetOriginDebugString(const base::Optional<url::Origin>& origin) {
  return origin ? origin->GetDebugString() : "null";
}

}  // namespace

NetworkIsolationKey::NetworkIsolationKey(const url::Origin& top_frame_origin,
                                         const url::Origin& frame_origin)
    : use_frame_origin_(base::FeatureList::IsEnabled(
          net::features::kAppendFrameOriginToNetworkIsolationKey)),
      top_frame_origin_(top_frame_origin) {
  if (use_frame_origin_) {
    frame_origin_ = frame_origin;
  }
}

NetworkIsolationKey::NetworkIsolationKey()
    : use_frame_origin_(base::FeatureList::IsEnabled(
          net::features::kAppendFrameOriginToNetworkIsolationKey)) {}

NetworkIsolationKey::NetworkIsolationKey(
    const NetworkIsolationKey& network_isolation_key) = default;

NetworkIsolationKey::~NetworkIsolationKey() = default;

NetworkIsolationKey& NetworkIsolationKey::operator=(
    const NetworkIsolationKey& network_isolation_key) = default;

NetworkIsolationKey& NetworkIsolationKey::operator=(
    NetworkIsolationKey&& network_isolation_key) = default;

std::string NetworkIsolationKey::ToString() const {
  if (IsTransient())
    return "";

  return top_frame_origin_->Serialize() +
         (use_frame_origin_ ? " " + frame_origin_->Serialize() : "");
}

std::string NetworkIsolationKey::ToDebugString() const {
  // The space-separated serialization of |top_frame_origin_| and
  // |frame_origin_|.
  std::string return_string = GetOriginDebugString(top_frame_origin_);
  if (use_frame_origin_) {
    return_string += " " + GetOriginDebugString(frame_origin_);
  }
  return return_string;
}

bool NetworkIsolationKey::IsFullyPopulated() const {
  return top_frame_origin_.has_value() &&
         (!use_frame_origin_ || frame_origin_.has_value());
}

bool NetworkIsolationKey::IsTransient() const {
  if (!IsFullyPopulated())
    return true;
  return top_frame_origin_->opaque() ||
         (use_frame_origin_ && frame_origin_->opaque());
}

bool NetworkIsolationKey::IsEmpty() const {
  return !top_frame_origin_.has_value();
}

}  // namespace net
