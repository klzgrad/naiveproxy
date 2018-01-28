// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Decodes the packet HandshakeFailureReason from the chromium histogram
// Net.QuicClientHelloRejectReasons

#include <iostream>

#include "base/command_line.h"
#include "base/strings/string_number_conversions.h"
#include "net/quic/core/crypto/crypto_handshake.h"
#include "net/quic/core/crypto/crypto_utils.h"

using base::CommandLine;
using net::HandshakeFailureReason;
using net::CryptoUtils;
using net::MAX_FAILURE_REASON;

int main(int argc, char* argv[]) {
  CommandLine::Init(argc, argv);
  CommandLine* line = CommandLine::ForCurrentProcess();
  const CommandLine::StringVector& args = line->GetArgs();

  if (args.size() != 1) {
    std::cerr << "Missing argument (Usage: " << argv[0] << " <packed_reason>\n";
    return 1;
  }

  uint32_t packed_error = 0;
  if (!base::StringToUint(args[0], &packed_error)) {
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
