// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Dumps the contents of a QUIC crypto handshake message in a human readable
// format.
//
// Usage: crypto_message_printer_bin <hex of message>

#include <iostream>
#include <string>
#include <vector>

#include "absl/strings/escaping.h"
#include "quiche/quic/core/crypto/crypto_framer.h"
#include "quiche/quic/core/quic_utils.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"

using std::cerr;
using std::cout;
using std::endl;

namespace quic {

class CryptoMessagePrinter : public ::quic::CryptoFramerVisitorInterface {
 public:
  void OnHandshakeMessage(const CryptoHandshakeMessage& message) override {
    cout << message.DebugString() << endl;
  }

  void OnError(CryptoFramer* framer) override {
    cerr << "Error code: " << framer->error() << endl;
    cerr << "Error details: " << framer->error_detail() << endl;
  }
};

}  // namespace quic

int main(int argc, char* argv[]) {
  const char* usage = "Usage: crypto_message_printer <hex>";
  std::vector<std::string> messages =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);
  if (messages.size() != 1) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    exit(0);
  }

  quic::CryptoMessagePrinter printer;
  quic::CryptoFramer framer;
  framer.set_visitor(&printer);
  framer.set_process_truncated_messages(true);
  std::string input;
  if (!absl::HexStringToBytes(messages[0], &input)) {
    cerr << "Invalid hex string provided" << endl;
    return 1;
  }
  if (!framer.ProcessInput(input)) {
    return 1;
  }
  if (framer.InputBytesRemaining() != 0) {
    cerr << "Input partially consumed. " << framer.InputBytesRemaining()
         << " bytes remaining." << endl;
    return 2;
  }
  return 0;
}
