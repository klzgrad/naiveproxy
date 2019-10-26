// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/tools/quic_toy_server.h"

#include <vector>

#include "net/third_party/quiche/src/quic/core/quic_versions.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_default_proof_providers.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_flags.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_ptr_util.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_socket_address.h"
#include "net/third_party/quiche/src/quic/tools/quic_memory_cache_backend.h"

DEFINE_QUIC_COMMAND_LINE_FLAG(int32_t,
                              port,
                              6121,
                              "The port the quic server will listen on.");

DEFINE_QUIC_COMMAND_LINE_FLAG(
    std::string,
    quic_response_cache_dir,
    "",
    "Specifies the directory used during QuicHttpResponseCache "
    "construction to seed the cache. Cache directory can be "
    "generated using `wget -p --save-headers <url>`");

DEFINE_QUIC_COMMAND_LINE_FLAG(bool,
                              quic_ietf_draft,
                              false,
                              "Use the IETF draft version. This also enables "
                              "required internal QUIC flags.");

namespace quic {

std::unique_ptr<quic::QuicSimpleServerBackend>
QuicToyServer::MemoryCacheBackendFactory::CreateBackend() {
  auto memory_cache_backend = QuicMakeUnique<QuicMemoryCacheBackend>();
  if (!GetQuicFlag(FLAGS_quic_response_cache_dir).empty()) {
    memory_cache_backend->InitializeBackend(
        GetQuicFlag(FLAGS_quic_response_cache_dir));
  }
  return memory_cache_backend;
}

QuicToyServer::QuicToyServer(BackendFactory* backend_factory,
                             ServerFactory* server_factory)
    : backend_factory_(backend_factory), server_factory_(server_factory) {}

int QuicToyServer::Start() {
  if (GetQuicFlag(FLAGS_quic_ietf_draft)) {
    QuicVersionInitializeSupportForIetfDraft();
    quic::QuicEnableVersion(
        quic::ParsedQuicVersion(quic::PROTOCOL_TLS1_3, quic::QUIC_VERSION_99));
  }
  auto proof_source = quic::CreateDefaultProofSource();
  auto backend = backend_factory_->CreateBackend();
  auto server =
      server_factory_->CreateServer(backend.get(), std::move(proof_source));

  if (!server->CreateUDPSocketAndListen(quic::QuicSocketAddress(
          quic::QuicIpAddress::Any6(), GetQuicFlag(FLAGS_port)))) {
    return 1;
  }

  server->HandleEventsForever();
  return 0;
}

}  // namespace quic
