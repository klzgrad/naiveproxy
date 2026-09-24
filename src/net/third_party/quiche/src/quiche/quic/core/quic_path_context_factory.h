// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_CORE_QUIC_PATH_CONTEXT_FACTORY_H_
#define QUICHE_QUIC_CORE_QUIC_PATH_CONTEXT_FACTORY_H_

#include <memory>

#include "absl/strings/string_view.h"
#include "quiche/quic/core/quic_force_blockable_packet_writer.h"
#include "quiche/quic/core/quic_path_validator.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/common/platform/api/quiche_export.h"

namespace quic {

// A client side interface that enforces the writer to be force blockable.
class QUICHE_EXPORT QuicClientPathValidationContext
    : public QuicPathValidationContext {
 public:
  QuicClientPathValidationContext(const QuicSocketAddress& self_address,
                                  const QuicSocketAddress& peer_address,
                                  QuicNetworkHandle network)
      : QuicClientPathValidationContext(self_address, peer_address,
                                        peer_address, network) {}

  QuicClientPathValidationContext(
      const QuicSocketAddress& self_address,
      const QuicSocketAddress& peer_address,
      const QuicSocketAddress& effective_peer_address,
      QuicNetworkHandle network)
      : QuicPathValidationContext(self_address, peer_address,
                                  effective_peer_address, network) {}

  QuicPacketWriter* WriterToUse() override {
    return ForceBlockableWriterToUse();
  }

  virtual QuicForceBlockablePacketWriter* ForceBlockableWriterToUse() = 0;
};

// An interface for creating QuicClientPathValidationContext objects used for
// probing and migrating path.
class QUICHE_EXPORT QuicPathContextFactory {
 public:
  // An interface to handle creation success and failure given the creation
  // might be asynchronous.
  class CreationResultDelegate {
   public:
    virtual ~CreationResultDelegate() = default;

    // Called when the manager successfully created a path context.
    // |context| must not be nullptr.
    virtual void OnCreationSucceeded(
        std::unique_ptr<QuicClientPathValidationContext> context) = 0;

    // Called when the manager fails to create a path context on |network|.
    // |error| will be populated with details.
    virtual void OnCreationFailed(QuicNetworkHandle network,
                                  absl::string_view error) = 0;
  };

  virtual ~QuicPathContextFactory() = default;

  // Create a QuicPathValidationContext instance on the given |network|
  // connecting to |peer_address|.
  // |result_delegate| can be called either in the current call stack or
  // asynchronously.
  virtual void CreatePathValidationContext(
      QuicNetworkHandle network, QuicSocketAddress peer_address,
      std::unique_ptr<CreationResultDelegate> result_delegate) = 0;
};

}  // namespace quic

#endif  // QUICHE_QUIC_CORE_QUIC_PATH_CONTEXT_FACTORY_H_
