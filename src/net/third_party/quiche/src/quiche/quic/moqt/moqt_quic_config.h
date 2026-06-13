// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MOQT_MOQT_QUIC_CONFIG_H_
#define QUICHE_QUIC_MOQT_MOQT_QUIC_CONFIG_H_

#include "quiche/quic/core/quic_config.h"

namespace moqt {

// Adjusts QuicConfig to work better with MOQT by setting parameters such as
// initial flow control windows appropriately.
void TuneQuicConfig(quic::QuicConfig& config);

// Convenience method that returns the result of tuning a default QuicConfig.
quic::QuicConfig GenerateQuicConfig();

}  // namespace moqt

#endif  // QUICHE_QUIC_MOQT_MOQT_QUIC_CONFIG_H_
