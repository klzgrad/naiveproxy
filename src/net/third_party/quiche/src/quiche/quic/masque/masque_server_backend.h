// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_SERVER_BACKEND_H_
#define QUICHE_QUIC_MASQUE_MASQUE_SERVER_BACKEND_H_

#include "absl/container/flat_hash_map.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/tools/quic_memory_cache_backend.h"
#include "quiche/spdy/core/http2_header_block.h"

namespace quic {

// QUIC server backend that understands MASQUE requests, but otherwise answers
// HTTP queries using an in-memory cache.
class QUIC_NO_EXPORT MasqueServerBackend : public QuicMemoryCacheBackend {
 public:
  // Interface meant to be implemented by the owner of the MasqueServerBackend
  // instance.
  class QUIC_NO_EXPORT BackendClient {
   public:
    virtual std::unique_ptr<QuicBackendResponse> HandleMasqueRequest(
        const spdy::Http2HeaderBlock& request_headers,
        QuicSimpleServerBackend::RequestHandler* request_handler) = 0;
    virtual ~BackendClient() = default;
  };

  explicit MasqueServerBackend(MasqueMode masque_mode,
                               const std::string& server_authority,
                               const std::string& cache_directory);

  // Disallow copy and assign.
  MasqueServerBackend(const MasqueServerBackend&) = delete;
  MasqueServerBackend& operator=(const MasqueServerBackend&) = delete;

  // From QuicMemoryCacheBackend.
  void FetchResponseFromBackend(
      const spdy::Http2HeaderBlock& request_headers,
      const std::string& request_body,
      QuicSimpleServerBackend::RequestHandler* request_handler) override;
  void HandleConnectHeaders(const spdy::Http2HeaderBlock& request_headers,
                            RequestHandler* request_handler) override;

  void CloseBackendResponseStream(
      QuicSimpleServerBackend::RequestHandler* request_handler) override;

  // Register backend client that can handle MASQUE requests.
  void RegisterBackendClient(QuicConnectionId connection_id,
                             BackendClient* backend_client);

  // Unregister backend client.
  void RemoveBackendClient(QuicConnectionId connection_id);

  // Provides a unique client IP address for each CONNECT-IP client.
  QuicIpAddress GetNextClientIpAddress();

 private:
  // Handle MASQUE request.
  bool MaybeHandleMasqueRequest(
      const spdy::Http2HeaderBlock& request_headers,
      QuicSimpleServerBackend::RequestHandler* request_handler);

  MasqueMode masque_mode_;
  std::string server_authority_;

  struct QUIC_NO_EXPORT BackendClientState {
    BackendClient* backend_client;
    std::vector<std::unique_ptr<QuicBackendResponse>> responses;
  };
  absl::flat_hash_map<QuicConnectionId, BackendClientState,
                      QuicConnectionIdHash>
      backend_client_states_;
  uint8_t connect_ip_next_client_ip_[4];
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_SERVER_BACKEND_H_
