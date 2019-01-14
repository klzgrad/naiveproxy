// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// A binary wrapper for QuicServer.  It listens forever on --port
// (default 6121) until it's killed or ctrl-cd to death.

#include <iostream>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/task_scheduler/task_scheduler.h"
#include "net/quic/crypto/proof_source_chromium.h"
#include "net/third_party/quic/core/quic_packets.h"
#include "net/third_party/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quic/platform/api/quic_string.h"
#include "net/third_party/quic/tools/quic_memory_cache_backend.h"
#include "net/third_party/quic/tools/quic_server.h"

// The port the quic server will listen on.
int32_t FLAGS_port = 6121;
// Mode of operations: currently only support in-memory cache
quic::QuicString FLAGS_quic_mode = "cache";
// Specifies the directory used during QuicHttpResponseCache
// construction to seed the cache. Cache directory can be
// generated using `wget -p --save-headers <url>`
quic::QuicString FLAGS_quic_response_cache_dir = "";

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
  base::CommandLine::Init(argc, argv);
  base::CommandLine* line = base::CommandLine::ForCurrentProcess();

  base::MessageLoopForIO message_loop;
  base::TaskScheduler::CreateAndStartWithDefaultParams("quic_server");

  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_SYSTEM_DEBUG_LOG;
  CHECK(logging::InitLogging(settings));

  if (line->HasSwitch("h") || line->HasSwitch("help")) {
    const char* help_str =
        "Usage: epoll_quic_server [options]\n"
        "\n"
        "Options:\n"
        "-h, --help                  show this help message and exit\n"
        "--port=<port>               specify the port to listen on\n"
        "--mode=<cache>              Default: cache\n"
        "                            Specify mode of operation: Cache will "
        "serve it "
        "from a cache dir\n"
        "--quic_response_cache_dir=<directory>\n"
        "                            The directory containing cached response "
        "data to load\n"
        "--certificate_file=<file>   path to the certificate chain\n"
        "--key_file=<file>           path to the pkcs8 private key\n";
    std::cout << help_str;
    exit(0);
  }

  quic::QuicMemoryCacheBackend memory_cache_backend;
  if (line->HasSwitch("mode")) {
    FLAGS_quic_mode = line->GetSwitchValueASCII("mode");
  }
  if (FLAGS_quic_mode.compare("cache") == 0) {
    if (line->HasSwitch("quic_response_cache_dir")) {
      FLAGS_quic_response_cache_dir =
          line->GetSwitchValueASCII("quic_response_cache_dir");
      if (FLAGS_quic_response_cache_dir.empty() ||
          memory_cache_backend.InitializeBackend(
              FLAGS_quic_response_cache_dir) != true) {
        LOG(ERROR) << "--quic_response_cache_dir is not valid !";
        return 1;
      }
    }
  } else {
    LOG(ERROR) << "unknown --mode. cache is a valid mode of operation";
    return 1;
  }

  if (line->HasSwitch("port")) {
    if (!base::StringToInt(line->GetSwitchValueASCII("port"), &FLAGS_port)) {
      LOG(ERROR) << "--port must be an integer\n";
      return 1;
    }
  }

  if (!line->HasSwitch("certificate_file")) {
    LOG(ERROR) << "missing --certificate_file";
    return 1;
  }

  if (!line->HasSwitch("key_file")) {
    LOG(ERROR) << "missing --key_file";
    return 1;
  }

  quic::QuicConfig config;
  quic::QuicServer server(
      CreateProofSource(line->GetSwitchValuePath("certificate_file"),
                        line->GetSwitchValuePath("key_file")),
      config, quic::QuicCryptoServerConfig::ConfigOptions(),
      quic::AllSupportedVersions(), &memory_cache_backend);

  int rc = server.CreateUDPSocketAndListen(
      quic::QuicSocketAddress(quic::QuicIpAddress::Any6(), FLAGS_port));
  if (rc < 0) {
    return 1;
  }

  while (true) {
    server.Start();
  }
}
