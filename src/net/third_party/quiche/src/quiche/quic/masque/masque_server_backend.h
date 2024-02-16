// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_MASQUE_MASQUE_SERVER_BACKEND_H_
#define QUICHE_QUIC_MASQUE_MASQUE_SERVER_BACKEND_H_

#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "openssl/curve25519.h"
#include "quiche/quic/core/quic_connection_id.h"
#include "quiche/quic/masque/masque_utils.h"
#include "quiche/quic/platform/api/quic_export.h"
#include "quiche/quic/platform/api/quic_ip_address.h"
#include "quiche/quic/tools/quic_backend_response.h"
#include "quiche/quic/tools/quic_memory_cache_backend.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"
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

  // Pass in a list of key identifiers and hex-encoded public keys, separated
  // with colons and semicolons. For example: "kid1:0123...f;kid2:0123...f".
  void SetSignatureAuth(absl::string_view signature_auth);

  // Returns whether any signature auth credentials are configured.
  bool IsSignatureAuthEnabled() const {
    return !signature_auth_credentials_.empty();
  }

  // If the key ID is known, copies the corresponding public key to
  // out_public_key and returns true. Otherwise returns false.
  bool GetSignatureAuthKeyForId(
      absl::string_view key_id,
      uint8_t out_public_key[ED25519_PUBLIC_KEY_LEN]) const;

  // Enable signature auth on all requests (e.g., GET) instead of just MASQUE.
  void SetSignatureAuthOnAllRequests(bool signature_auth_on_all_requests) {
    signature_auth_on_all_requests_ = signature_auth_on_all_requests;
  }

  // Whether signature auth is enabled on all requests (e.g., GET).
  bool IsSignatureAuthOnAllRequests() const {
    return signature_auth_on_all_requests_;
  }

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
  struct QUIC_NO_EXPORT SignatureAuthCredential {
    std::string key_id;
    uint8_t public_key[ED25519_PUBLIC_KEY_LEN];
  };
  std::list<SignatureAuthCredential> signature_auth_credentials_;
  bool signature_auth_on_all_requests_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_MASQUE_MASQUE_SERVER_BACKEND_H_
