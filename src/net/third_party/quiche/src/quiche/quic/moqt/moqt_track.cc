// Copyright 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

#include "quiche/quic/moqt/moqt_track.h"

#include <cstdint>

#include "quiche/quic/moqt/moqt_messages.h"

namespace moqt {

bool RemoteTrack::CheckForwardingPreference(
    MoqtForwardingPreference preference) {
  if (forwarding_preference_.has_value()) {
    return forwarding_preference_.value() == preference;
  }
  forwarding_preference_ = preference;
  return true;
}

}  // namespace moqt
