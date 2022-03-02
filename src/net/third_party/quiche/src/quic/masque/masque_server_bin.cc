// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is reponsible for the masque_server binary. It allows testing
// our MASQUE server code by creating a MASQUE proxy that relays HTTP/3
// requests to web servers tunnelled over MASQUE connections.
// e.g.: masque_server

#include <memory>

#include "quic/masque/masque_epoll_server.h"
#include "quic/masque/masque_server_backend.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_logging.h"
#include "quic/platform/api/quic_socket_address.h"
#include "quic/platform/api/quic_system_event_loop.h"

DEFINE_QUIC_COMMAND_LINE_FLAG(int32_t,
                              port,
                              9661,
                              "The port the MASQUE server will listen on.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    std::string,
    cache_dir,
    "",
    "Specifies the directory used during QuicHttpResponseCache "
    "construction to seed the cache. Cache directory can be "
    "generated using `wget -p --save-headers <url>`");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    std::string,
    server_authority,
    "",
    "Specifies the authority over which the server will accept MASQUE "
    "requests. Defaults to empty which allows all authorities.");

DEFINE_QUIC_COMMAND_LINE_FLAG(std::string,
                              masque_mode,
                              "",
                              "Allows setting MASQUE mode, valid values are "
                              "open and legacy. Defaults to open.");

int main(int argc, char* argv[]) {
  QuicSystemEventLoop event_loop("masque_server");
  const char* usage = "Usage: masque_server [options]";
  std::vector<std::string> non_option_args =
      quic::QuicParseCommandLineFlags(usage, argc, argv);
  if (!non_option_args.empty()) {
    quic::QuicPrintCommandLineFlagHelp(usage);
    return 0;
  }

  quic::MasqueMode masque_mode = quic::MasqueMode::kOpen;
  std::string mode_string = GetQuicFlag(FLAGS_masque_mode);
  if (mode_string == "legacy") {
    masque_mode = quic::MasqueMode::kLegacy;
  } else if (!mode_string.empty() && mode_string != "open") {
    std::cerr << "Invalid masque_mode \"" << mode_string << "\"" << std::endl;
    return 1;
  }

  auto backend = std::make_unique<quic::MasqueServerBackend>(
      masque_mode, GetQuicFlag(FLAGS_server_authority),
      GetQuicFlag(FLAGS_cache_dir));

  auto server =
      std::make_unique<quic::MasqueEpollServer>(masque_mode, backend.get());

  if (!server->CreateUDPSocketAndListen(quic::QuicSocketAddress(
          quic::QuicIpAddress::Any6(), GetQuicFlag(FLAGS_port)))) {
    return 1;
  }

  std::cerr << "Started " << masque_mode << " MASQUE server" << std::endl;
  server->HandleEventsForever();
  return 0;
}
