// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_SERVER_BACKEND_H_
#define QUICHE_QUIC_MASQUE_MASQUE_SERVER_BACKEND_H_

#include "net/third_party/quiche/src/quic/platform/api/quic_export.h"
#include "net/third_party/quiche/src/quic/tools/quic_memory_cache_backend.h"

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
        const std::string& masque_path,
        const spdy::SpdyHeaderBlock& request_headers,
        const std::string& request_body,
        QuicSimpleServerBackend::RequestHandler* request_handler) = 0;
    virtual ~BackendClient() = default;
  };

  explicit MasqueServerBackend(const std::string& server_authority,
                               const std::string& cache_directory);

  // Disallow copy and assign.
  MasqueServerBackend(const MasqueServerBackend&) = delete;
  MasqueServerBackend& operator=(const MasqueServerBackend&) = delete;

  // From QuicMemoryCacheBackend.
  void FetchResponseFromBackend(
      const spdy::SpdyHeaderBlock& request_headers,
      const std::string& request_body,
      QuicSimpleServerBackend::RequestHandler* request_handler) override;

  void CloseBackendResponseStream(
      QuicSimpleServerBackend::RequestHandler* request_handler) override;

  // Register backend client that can handle MASQUE requests.
  void RegisterBackendClient(QuicConnectionId connection_id,
                             BackendClient* backend_client);

  // Unregister backend client.
  void RemoveBackendClient(QuicConnectionId connection_id);

 private:
  // Handle MASQUE request.
  bool MaybeHandleMasqueRequest(
      const spdy::SpdyHeaderBlock& request_headers,
      const std::string& request_body,
      QuicSimpleServerBackend::RequestHandler* request_handler);

  std::string server_authority_;
  QuicUnorderedMap<std::string, std::unique_ptr<QuicBackendResponse>>
      active_response_map_;
  QuicUnorderedMap<QuicConnectionId, BackendClient*, QuicConnectionIdHash>
      backend_clients_;
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_SERVER_BACKEND_H_
