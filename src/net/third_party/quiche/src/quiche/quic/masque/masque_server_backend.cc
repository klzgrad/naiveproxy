// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/masque/masque_server_backend.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace quic {

MasqueServerBackend::MasqueServerBackend(MasqueMode masque_mode,
                                         const std::string& server_authority,
                                         const std::string& cache_directory)
    : masque_mode_(masque_mode), server_authority_(server_authority) {
  // Start with client IP 10.1.1.2.
  connect_ip_next_client_ip_[0] = 10;
  connect_ip_next_client_ip_[1] = 1;
  connect_ip_next_client_ip_[2] = 1;
  connect_ip_next_client_ip_[3] = 2;

  if (!cache_directory.empty()) {
    QuicMemoryCacheBackend::InitializeBackend(cache_directory);
  }
}

bool MasqueServerBackend::MaybeHandleMasqueRequest(
    const spdy::Http2HeaderBlock& request_headers,
    QuicSimpleServerBackend::RequestHandler* request_handler) {
  auto method_pair = request_headers.find(":method");
  if (method_pair == request_headers.end()) {
    // Request is missing a method.
    return false;
  }
  absl::string_view method = method_pair->second;
  std::string masque_path = "";
  auto protocol_pair = request_headers.find(":protocol");
  if (method != "CONNECT" || protocol_pair == request_headers.end() ||
      (protocol_pair->second != "connect-udp" &&
       protocol_pair->second != "connect-ip" &&
       protocol_pair->second != "connect-ethernet")) {
    // This is not a MASQUE request.
    return false;
  }

  if (!server_authority_.empty()) {
    auto authority_pair = request_headers.find(":authority");
    if (authority_pair == request_headers.end()) {
      // Cannot enforce missing authority.
      return false;
    }
    absl::string_view authority = authority_pair->second;
    if (server_authority_ != authority) {
      // This request does not match server_authority.
      return false;
    }
  }

  auto it = backend_client_states_.find(request_handler->connection_id());
  if (it == backend_client_states_.end()) {
    QUIC_LOG(ERROR) << "Could not find backend client for " << masque_path
                    << request_headers.DebugString();
    return false;
  }

  BackendClient* backend_client = it->second.backend_client;

  std::unique_ptr<QuicBackendResponse> response =
      backend_client->HandleMasqueRequest(request_headers, request_handler);
  if (response == nullptr) {
    QUIC_LOG(ERROR) << "Backend client did not process request for "
                    << masque_path << request_headers.DebugString();
    return false;
  }

  QUIC_DLOG(INFO) << "Sending MASQUE response for "
                  << request_headers.DebugString();

  request_handler->OnResponseBackendComplete(response.get());
  it->second.responses.emplace_back(std::move(response));

  return true;
}

void MasqueServerBackend::FetchResponseFromBackend(
    const spdy::Http2HeaderBlock& request_headers,
    const std::string& request_body,
    QuicSimpleServerBackend::RequestHandler* request_handler) {
  if (MaybeHandleMasqueRequest(request_headers, request_handler)) {
    // Request was handled as a MASQUE request.
    return;
  }
  QUIC_DLOG(INFO) << "Fetching non-MASQUE response for "
                  << request_headers.DebugString();
  QuicMemoryCacheBackend::FetchResponseFromBackend(
      request_headers, request_body, request_handler);
}

void MasqueServerBackend::HandleConnectHeaders(
    const spdy::Http2HeaderBlock& request_headers,
    RequestHandler* request_handler) {
  if (MaybeHandleMasqueRequest(request_headers, request_handler)) {
    // Request was handled as a MASQUE request.
    return;
  }
  QUIC_DLOG(INFO) << "Fetching non-MASQUE CONNECT response for "
                  << request_headers.DebugString();
  QuicMemoryCacheBackend::HandleConnectHeaders(request_headers,
                                               request_handler);
}

void MasqueServerBackend::CloseBackendResponseStream(
    QuicSimpleServerBackend::RequestHandler* request_handler) {
  QUIC_DLOG(INFO) << "Closing response stream";
  QuicMemoryCacheBackend::CloseBackendResponseStream(request_handler);
}

void MasqueServerBackend::RegisterBackendClient(QuicConnectionId connection_id,
                                                BackendClient* backend_client) {
  QUIC_DLOG(INFO) << "Registering backend client for " << connection_id;
  QUIC_BUG_IF(quic_bug_12005_1, backend_client_states_.find(connection_id) !=
                                    backend_client_states_.end())
      << connection_id << " already in backend clients map";
  backend_client_states_[connection_id] =
      BackendClientState{backend_client, {}};
}

void MasqueServerBackend::RemoveBackendClient(QuicConnectionId connection_id) {
  QUIC_DLOG(INFO) << "Removing backend client for " << connection_id;
  backend_client_states_.erase(connection_id);
}

QuicIpAddress MasqueServerBackend::GetNextClientIpAddress() {
  // Makes sure all addresses are in 10.(1-254).(1-254).(2-254)
  QuicIpAddress address;
  address.FromPackedString(
      reinterpret_cast<char*>(&connect_ip_next_client_ip_[0]),
      sizeof(connect_ip_next_client_ip_));
  connect_ip_next_client_ip_[3]++;
  if (connect_ip_next_client_ip_[3] >= 255) {
    connect_ip_next_client_ip_[3] = 2;
    connect_ip_next_client_ip_[2]++;
    if (connect_ip_next_client_ip_[2] >= 255) {
      connect_ip_next_client_ip_[2] = 1;
      connect_ip_next_client_ip_[1]++;
      if (connect_ip_next_client_ip_[1] >= 255) {
        QUIC_LOG(FATAL) << "Ran out of IP addresses, restarting process.";
      }
    }
  }
  return address;
}

}  // namespace quic
