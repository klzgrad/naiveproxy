// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_QBONE_MOCK_QBONE_CLIENT_H_
#define QUICHE_QUIC_QBONE_MOCK_QBONE_CLIENT_H_

#include "absl/strings/string_view.h"
#include "quiche/quic/platform/api/quic_test.h"
#include "quiche/quic/qbone/qbone_client_interface.h"

namespace quic {

class MockQboneClient : public QboneClientInterface {
 public:
  MOCK_METHOD(void, ProcessPacketFromNetwork, (absl::string_view packet),
              (override));
};

}  // namespace quic

#endif  // QUICHE_QUIC_QBONE_MOCK_QBONE_CLIENT_H_
