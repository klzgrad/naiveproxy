// Copyright (c) 2025 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <poll.h>
#include <unistd.h>

#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "quiche/quic/moqt/tools/moqt_relay.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_default_proof_providers.h"

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    bool, disable_certificate_verification, false,
    "If true, don't verify the server certificate.");

DEFINE_QUICHE_COMMAND_LINE_FLAG(std::string, bind_address, "127.0.0.1",
                                "Local IP address to bind to");

DEFINE_QUICHE_COMMAND_LINE_FLAG(uint16_t, port, 9667,
                                "Port for the server to listen on");

DEFINE_QUICHE_COMMAND_LINE_FLAG(
    std::string, default_upstream, "",
    "If set, connect to the upstream URL and forward all requests there if "
    "there is no explicitly advertised source.");

// A pure MoQT relay. Accepts connections. Will try to route requests from a
// session to a different appropriate upstream session. If the namespace for the
// request has not been advertised, it will reject the request. If
// |default_upstream| is set, it connects on startup to that hosts, and forwards
// such requests there instead.
int main(int argc, char* argv[]) {
  const char* usage = "Usage: moqt_relay [options]";
  std::vector<std::string> args =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);
  if (!args.empty()) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    return 1;
  }
  moqt::MoqtRelay relay(
      quiche::CreateDefaultProofSource(),
      quiche::GetQuicheCommandLineFlag(FLAGS_bind_address),
      quiche::GetQuicheCommandLineFlag(FLAGS_port),
      quiche::GetQuicheCommandLineFlag(FLAGS_default_upstream),
      quiche::GetQuicheCommandLineFlag(FLAGS_disable_certificate_verification));
  relay.HandleEventsForever();
  return 0;
}
