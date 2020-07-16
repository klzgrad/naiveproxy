// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is reponsible for the masque_server binary. It allows testing
// our MASQUE server code by creating a MASQUE proxy that relays HTTP/3
// requests to web servers tunnelled over MASQUE connections.
// e.g.: masque_server

#include <memory>

#include "net/third_party/quiche/src/quic/masque/masque_epoll_server.h"
#include "net/third_party/quiche/src/quic/masque/masque_server_backend.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_logging.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"

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

int main(int argc, char* argv[]) {
  const char* usage = "Usage: masque_server [options]";
  std::vector<std::string> non_option_args =
      quic::QuicParseCommandLineFlags(usage, argc, argv);
  if (!non_option_args.empty()) {
    quic::QuicPrintCommandLineFlagHelp(usage);
    return 0;
  }

  auto backend = std::make_unique<quic::MasqueServerBackend>(
      GetQuicFlag(FLAGS_server_authority), GetQuicFlag(FLAGS_cache_dir));

  auto server = std::make_unique<quic::MasqueEpollServer>(backend.get());

  if (!server->CreateUDPSocketAndListen(quic::QuicSocketAddress(
          quic::QuicIpAddress::Any6(), GetQuicFlag(FLAGS_port)))) {
    return 1;
  }

  std::cerr << "Started MASQUE server" << std::endl;
  server->HandleEventsForever();
  return 0;
}
