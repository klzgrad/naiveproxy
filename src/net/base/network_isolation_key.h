// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NETWORK_ISOLATION_KEY_H_
#define NET_BASE_NETWORK_ISOLATION_KEY_H_

#include <string>

#include "base/macros.h"
#include "base/optional.h"
#include "net/base/net_export.h"
#include "url/origin.h"

namespace net {

// Key used to isolate shared network stack resources used by requests based on
// the context on which they were made.
class NET_EXPORT NetworkIsolationKey {
 public:
  NetworkIsolationKey(const base::Optional<url::Origin>& top_frame_origin);

  // Construct an empty key.
  NetworkIsolationKey();

  NetworkIsolationKey(const NetworkIsolationKey& network_isolation_key);

  ~NetworkIsolationKey();

  NetworkIsolationKey& operator=(
      const NetworkIsolationKey& network_isolation_key);
  NetworkIsolationKey& operator=(NetworkIsolationKey&& network_isolation_key);

  bool operator==(const NetworkIsolationKey& other) const {
    return top_frame_origin_ == other.top_frame_origin_;
  }

  bool operator<(const NetworkIsolationKey& other) const {
    return top_frame_origin_ < other.top_frame_origin_;
  }

  // TODO(shivanisha): Use feature flags in the below methods to determine which
  // parts of the key are being used based on the enabled experiment.

  // Returns the string representation of the key.
  std::string ToString() const;

  // Returns string for debugging. Difference from ToString() is that transient
  // entries may be distinguishable from each other.
  std::string ToDebugString() const;

  // Returns true if all parts of the key are non-empty.
  bool IsFullyPopulated() const;

  // Returns true if this key's lifetime is short-lived. It may not make sense
  // to persist state to disk related to it (e.g., disk cache).
  bool IsTransient() const;

 private:
  // The origin of the top frame of the request (if applicable).
  base::Optional<url::Origin> top_frame_origin_;

  // TODO(crbug.com/950069): Also add initiator origin to the key.
};

}  // namespace net

#endif  // NET_BASE_NETWORK_ISOLATION_KEY_H_
