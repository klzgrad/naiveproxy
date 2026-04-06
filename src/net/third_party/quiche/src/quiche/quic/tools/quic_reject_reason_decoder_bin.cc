// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Decodes the packet HandshakeFailureReason from the chromium histogram
// Net.QuicClientHelloRejectReasons

#include <iostream>
#include <string>
#include <vector>

#include "absl/strings/numbers.h"
#include "quiche/quic/core/crypto/crypto_handshake.h"
#include "quiche/quic/core/crypto/crypto_utils.h"
#include "quiche/quic/platform/api/quic_flags.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/quiche_text_utils.h"

using quic::CryptoUtils;
using quic::HandshakeFailureReason;
using quic::MAX_FAILURE_REASON;

int main(int argc, char* argv[]) {
  const char* usage = "Usage: quic_reject_reason_decoder <packed_reason>";
  std::vector<std::string> args =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);

  if (args.size() != 1) {
    std::cerr << usage << std::endl;
    return 1;
  }

  uint32_t packed_error = 0;
  if (!absl::SimpleAtoi(args[0], &packed_error)) {
    std::cerr << "Unable to parse: " << args[0] << "\n";
    return 2;
  }

  for (int i = 1; i < MAX_FAILURE_REASON; ++i) {
    if ((packed_error & (1 << (i - 1))) == 0) {
      continue;
    }
    HandshakeFailureReason reason = static_cast<HandshakeFailureReason>(i);
    std::cout << CryptoUtils::HandshakeFailureReasonToString(reason) << "\n";
  }
  return 0;
}
