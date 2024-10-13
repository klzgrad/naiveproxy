// Copyright (c) 2024 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <string>
#include <vector>

#include "quiche/quic/moqt/tools/chat_server.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_default_proof_providers.h"
#include "quiche/common/platform/api/quiche_logging.h"
#include "quiche/common/quiche_ip_address.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, output_file, "",
    "chat messages will stream to a file instead of stdout");
DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, bind_address, "127.0.0.1",
                                "Local IP address to bind to");
DEFINE_QUICHE_COMMAND_LINE_FLAG(uint16_t, port, 9667,
                                "Port for the server to listen on");

// A server for MoQT over chat, used for interop testing. See
// https://afrind.github.io/draft-frindell-moq-chat/draft-frindell-moq-chat.html
int main(int argc, char* argv[]) {
  const char* usage = "Usage: chat_server [options] <chat-id>";
  std::vector<std::string> args =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);
  if (args.size() != 1) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    return 1;
  }
  moqt::ChatServer server(quiche::CreateDefaultProofSource(), argv[1],
                          quiche::GetQuicheCommandLineFlag(FLAGS_output_file));
  quiche::QuicheIpAddress bind_address;
  QUICHE_CHECK(bind_address.FromString(
      quiche::GetQuicheCommandLineFlag(FLAGS_bind_address)));
  server.moqt_server().quic_server().CreateUDPSocketAndListen(
      quic::QuicSocketAddress(bind_address,
                              quiche::GetQuicheCommandLineFlag(FLAGS_port)));
  server.moqt_server().quic_server().HandleEventsForever();
}
