// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A binary wrapper for QuicServer.  It listens forever on --port
// (default 6121) until it's killed or ctrl-cd to death.

#include <string>
#include <vector>

#include "quiche/quic/tools/quic_server_factory.h"
#include "quiche/quic/tools/quic_toy_server.h"
#include "quiche/common/platform/api/quiche_command_line_flags.h"
#include "quiche/common/platform/api/quiche_system_event_loop.h"

int main(int argc, char* argv[]) {
  quiche::QuicheSystemEventLoop event_loop("quic_server");
  const char* usage = "Usage: quic_server [options]";
  std::vector<std::string> non_option_args =
      quiche::QuicheParseCommandLineFlags(usage, argc, argv);
  if (!non_option_args.empty()) {
    quiche::QuichePrintCommandLineFlagHelp(usage);
    exit(0);
  }

  quic::QuicToyServer::MemoryCacheBackendFactory backend_factory;
  quic::QuicServerFactory server_factory;
  quic::QuicToyServer server(&backend_factory, &server_factory);
  return server.Start();
}
