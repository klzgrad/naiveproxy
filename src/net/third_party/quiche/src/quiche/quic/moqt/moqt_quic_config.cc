// Copyright 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/moqt/moqt_quic_config.h"

#include "quiche/quic/core/crypto/crypto_protocol.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/core/quic_types.h"
#include "quiche/quic/platform/api/quic_flags.h"

namespace moqt {

namespace {

using ::quic::QuicByteCount;

// Since MoQT creates a lot of short-lived streams, the tuning of the initial
// stream flow control window is critical to avoid incurring a latency penalty
// from ramping up the window on every individual stream.
//
// A typical I-frame in a high-bitrate FHD video tends to be in the low 100 KiB
// range. Even for a higher-latency connection such as 100ms, that would imply
// an instantaneous bitrate of 10 Mbps.
constexpr QuicByteCount kDefaultInitialStreamWindow = 512 * 1024;

// The flow control window autotuning does work with connection-level flow
// control, but we still can make the startup smoother by setting a more
// reasonable value than the default 16 KiB.  It does not have to accomodate for
// much more than a single data stream at a time, since for most of the MOQT
// users the bandwidth usage would be dominated by a single track.
constexpr QuicByteCount kDefaultInitialConnectionWindow =
    3 * kDefaultInitialStreamWindow;

}  // namespace

void TuneQuicConfig(quic::QuicConfig& config) {
  config.SetInitialMaxStreamDataBytesUnidirectionalToSend(
      kDefaultInitialStreamWindow);
  config.SetInitialSessionFlowControlWindowToSend(
      kDefaultInitialConnectionWindow);

  // Enable BBRv1 along with a workaround for an issue that MOQT can hit often.
  SetQuicReloadableFlag(quic_bbr_exit_startup_on_loss, true);
  config.AddConnectionOptionsToSend({quic::kTBBR, quic::kB1AL});
  config.SetClientConnectionOptions({quic::kTBBR, quic::kB1AL});
}

quic::QuicConfig GenerateQuicConfig() {
  quic::QuicConfig config;
  TuneQuicConfig(config);
  return config;
}

}  // namespace moqt
