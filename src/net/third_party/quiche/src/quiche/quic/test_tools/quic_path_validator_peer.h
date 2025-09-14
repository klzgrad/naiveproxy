// Copyright (c) 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_QUIC_PATH_VALIDATOR_PEER_H_
#define QUICHE_QUIC_TEST_TOOLS_QUIC_PATH_VALIDATOR_PEER_H_

#include "quiche/quic/core/quic_path_validator.h"

namespace quic {
namespace test {

class QuicPathValidatorPeer {
 public:
  static QuicAlarm* retry_timer(QuicPathValidator* validator);
};

}  // namespace test
}  // namespace quic
#endif  // QUICHE_QUIC_TEST_TOOLS_QUIC_PATH_VALIDATOR_PEER_H_
