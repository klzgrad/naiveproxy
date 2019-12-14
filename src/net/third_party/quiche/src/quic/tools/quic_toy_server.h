// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_TOY_SERVER_H_
#define QUICHE_QUIC_TOOLS_QUIC_TOY_SERVER_H_

#include "net/third_party/quiche/src/quic/core/crypto/proof_source.h"
#include "net/third_party/quiche/src/quic/tools/quic_simple_server_backend.h"
#include "net/third_party/quiche/src/quic/tools/quic_spdy_server_base.h"

namespace quic {

// A binary wrapper for QuicServer.  It listens forever on --port
// (default 6121) until it's killed or ctrl-cd to death.
class QuicToyServer {
 public:
  // A factory for creating QuicSpdyServerBase instances.
  class ServerFactory {
   public:
    virtual ~ServerFactory() = default;

    // Creates a QuicSpdyServerBase instance using |backend| for generating
    // responses, and |proof_source| for certificates.
    virtual std::unique_ptr<QuicSpdyServerBase> CreateServer(
        QuicSimpleServerBackend* backend,
        std::unique_ptr<ProofSource> proof_source,
        const ParsedQuicVersionVector& supported_versions) = 0;
  };

  // A facotry for creating QuicSimpleServerBackend instances.
  class BackendFactory {
   public:
    virtual ~BackendFactory() = default;

    // Creates a new backend.
    virtual std::unique_ptr<QuicSimpleServerBackend> CreateBackend() = 0;
  };

  // A factory for creating QuicMemoryCacheBackend instances, configured
  // to load files from disk, if necessary.
  class MemoryCacheBackendFactory : public BackendFactory {
   public:
    std::unique_ptr<quic::QuicSimpleServerBackend> CreateBackend() override;
  };

  // Constructs a new toy server that will use |server_factory| to create the
  // actual QuicSpdyServerBase instance.
  QuicToyServer(BackendFactory* backend_factory, ServerFactory* server_factory);

  // Connects to the QUIC server based on the various flags defined in the
  // .cc file, listends for requests and sends the responses. Returns 1 on
  // failure and does not return otherwise.
  int Start();

 private:
  BackendFactory* backend_factory_;  // Unowned.
  ServerFactory* server_factory_;    // Unowned.
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_TOY_SERVER_H_
