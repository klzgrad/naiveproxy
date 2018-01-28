// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/test_tools/simulator/port.h"

using std::string;

namespace net {
namespace simulator {

Packet::Packet()
    : source(), destination(), tx_timestamp(QuicTime::Zero()), size(0) {}

Packet::~Packet() {}

Packet::Packet(const Packet& packet) = default;

Endpoint::Endpoint(Simulator* simulator, string name)
    : Actor(simulator, name) {}

}  // namespace simulator
}  // namespace net
