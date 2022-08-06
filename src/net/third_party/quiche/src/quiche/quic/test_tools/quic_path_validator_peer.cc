// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/quic_path_validator_peer.h"

namespace quic {
namespace test {
//  static
QuicAlarm* QuicPathValidatorPeer::retry_timer(QuicPathValidator* validator) {
  return validator->retry_timer_.get();
}

}  // namespace test
}  // namespace quic
