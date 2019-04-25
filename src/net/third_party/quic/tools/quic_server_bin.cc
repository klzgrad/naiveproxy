// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A binary wrapper for QuicServer.  It listens forever on --port
// (default 6121) until it's killed or ctrl-cd to death.

#include <iostream>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/task_scheduler/task_scheduler.h"
#include "net/quic/crypto/proof_source_chromium.h"
#include "net/third_party/quic/core/quic_packets.h"
#include "net/third_party/quic/platform/api/quic_flags.h"
#include "net/third_party/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/tools/quic_memory_cache_backend.h"
#include "net/third_party/quic/tools/quic_server.h"

DEFINE_QUIC_COMMAND_LINE_FLAG(int32_t,
                              port,
                              6121,
                              "The port the quic server will listen on.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    quic::QuicString,
    mode,
    "cache",
    "Mode of operations: currently only support in-memory cache.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    quic::QuicString,
    quic_response_cache_dir,
    "",
    "Specifies the directory used during QuicHttpResponseCache construction to "
    "seed the cache. Cache directory can be generated using `wget -p "
    "--save-headers <url>`");

DEFINE_QUIC_COMMAND_LINE_FLAG(quic::QuicString,
                              certificate_file,
                              "",
                              "Path to the certificate chain.");

DEFINE_QUIC_COMMAND_LINE_FLAG(quic::QuicString,
                              key_file,
                              "",
                              "Path to the pkcs8 private key.");

std::unique_ptr<quic::ProofSource> CreateProofSource(
    const base::FilePath& cert_path,
    const base::FilePath& key_path) {
  std::unique_ptr<net::ProofSourceChromium> proof_source(
      new net::ProofSourceChromium());
  CHECK(proof_source->Initialize(cert_path, key_path, base::FilePath()));
  return std::move(proof_source);
}

int main(int argc, char* argv[]) {
  base::AtExitManager exit_manager;
  const char* usage = "Usage: epoll_quic_server [options]\n";
  quic::QuicParseCommandLineFlags(usage, argc, argv);

  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  CHECK(logging::InitLogging(settings));

  quic::QuicMemoryCacheBackend memory_cache_backend;
  if (GetQuicFlag(FLAGS_mode) == "cache") {
    if (GetQuicFlag(FLAGS_quic_response_cache_dir).empty() ||
        !memory_cache_backend.InitializeBackend(
            GetQuicFlag(FLAGS_quic_response_cache_dir))) {
      LOG(ERROR) << "--quic_response_cache_dir is not valid !";
      return 1;
    }
  } else {
    LOG(ERROR) << "unknown --mode. cache is a valid mode of operation";
    return 1;
  }

  if (GetQuicFlag(FLAGS_certificate_file).empty()) {
    LOG(ERROR) << "missing --certificate_file";
    return 1;
  }

  if (GetQuicFlag(FLAGS_key_file).empty()) {
    LOG(ERROR) << "missing --key_file";
    return 1;
  }

  quic::QuicConfig config;
  quic::QuicServer server(
      CreateProofSource(base::FilePath(GetQuicFlag(FLAGS_certificate_file)),
                        base::FilePath(GetQuicFlag(FLAGS_key_file))),
      config, quic::QuicCryptoServerConfig::ConfigOptions(),
      quic::AllSupportedVersions(), &memory_cache_backend);

  int rc = server.CreateUDPSocketAndListen(quic::QuicSocketAddress(
      quic::QuicIpAddress::Any6(), GetQuicFlag(FLAGS_port)));
  if (rc < 0) {
    return 1;
  }

  while (true) {
    server.WaitForEvents();
  }
}
