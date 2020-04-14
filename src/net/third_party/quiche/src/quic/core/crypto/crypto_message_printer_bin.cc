// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Dumps the contents of a QUIC crypto handshake message in a human readable
// format.
//
// Usage: crypto_message_printer_bin <hex of message>

#include <iostream>
#include <string>

#include "net/third_party/quiche/src/quic/core/crypto/crypto_framer.h"
#include "net/third_party/quiche/src/quic/core/quic_utils.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_text_utils.h"

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
      quic::QuicParseCommandLineFlags(usage, argc, argv);
  if (messages.size() != 1) {
    quic::QuicPrintCommandLineFlagHelp(usage);
    exit(0);
  }

  quic::CryptoMessagePrinter printer;
  quic::CryptoFramer framer;
  framer.set_visitor(&printer);
  framer.set_process_truncated_messages(true);
  std::string input = quiche::QuicheTextUtils::HexDecode(messages[0]);
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
