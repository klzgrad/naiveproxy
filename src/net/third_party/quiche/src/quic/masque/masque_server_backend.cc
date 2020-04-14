// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/masque/masque_server_backend.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_str_cat.h"
#include "net/third_party/quiche/src/common/platform/api/quiche_string_piece.h"

namespace quic {

namespace {

std::string GetRequestHandlerKey(
    const QuicSimpleServerBackend::RequestHandler* request_handler) {
  return quiche::QuicheStrCat(request_handler->connection_id().ToString(), "_",
                              request_handler->stream_id(), "_",
                              request_handler->peer_host());
}

}  // namespace

MasqueServerBackend::MasqueServerBackend(const std::string& server_authority,
                                         const std::string& cache_directory)
    : server_authority_(server_authority) {
  if (!cache_directory.empty()) {
    QuicMemoryCacheBackend::InitializeBackend(cache_directory);
  }
}

bool MasqueServerBackend::MaybeHandleMasqueRequest(
    const spdy::SpdyHeaderBlock& request_headers,
    const std::string& request_body,
    QuicSimpleServerBackend::RequestHandler* request_handler) {
  auto path_pair = request_headers.find(":path");
  auto method_pair = request_headers.find(":method");
  auto scheme_pair = request_headers.find(":scheme");
  if (path_pair == request_headers.end() ||
      method_pair == request_headers.end() ||
      scheme_pair == request_headers.end()) {
    // This request is missing required headers.
    return false;
  }
  quiche::QuicheStringPiece path = path_pair->second;
  quiche::QuicheStringPiece scheme = scheme_pair->second;
  quiche::QuicheStringPiece method = method_pair->second;
  if (scheme != "https" || method != "POST" || request_body.empty()) {
    // MASQUE requests MUST be a non-empty https POST.
    return false;
  }

  if (path.rfind("/.well-known/masque/", 0) != 0) {
    // This request is not a MASQUE path.
    return false;
  }
  std::string masque_path(path.substr(sizeof("/.well-known/masque/") - 1));

  if (!server_authority_.empty()) {
    auto authority_pair = request_headers.find(":authority");
    if (authority_pair == request_headers.end()) {
      // Cannot enforce missing authority.
      return false;
    }
    quiche::QuicheStringPiece authority = authority_pair->second;
    if (server_authority_ != authority) {
      // This request does not match server_authority.
      return false;
    }
  }

  auto backend_client_pair =
      backend_clients_.find(request_handler->connection_id());
  if (backend_client_pair == backend_clients_.end()) {
    QUIC_LOG(ERROR) << "Could not find backend client for "
                    << GetRequestHandlerKey(request_handler) << " "
                    << masque_path << request_headers.DebugString();
    return false;
  }

  BackendClient* backend_client = backend_client_pair->second;

  std::unique_ptr<QuicBackendResponse> response =
      backend_client->HandleMasqueRequest(masque_path, request_headers,
                                          request_body, request_handler);
  if (response == nullptr) {
    QUIC_LOG(ERROR) << "Backend client did not process request for "
                    << GetRequestHandlerKey(request_handler) << " "
                    << masque_path << request_headers.DebugString();
    return false;
  }

  QUIC_DLOG(INFO) << "Sending MASQUE response for "
                  << GetRequestHandlerKey(request_handler) << " " << masque_path
                  << request_headers.DebugString();

  request_handler->OnResponseBackendComplete(response.get(), {});
  active_response_map_[GetRequestHandlerKey(request_handler)] =
      std::move(response);

  return true;
}

void MasqueServerBackend::FetchResponseFromBackend(
    const spdy::SpdyHeaderBlock& request_headers,
    const std::string& request_body,
    QuicSimpleServerBackend::RequestHandler* request_handler) {
  if (MaybeHandleMasqueRequest(request_headers, request_body,
                               request_handler)) {
    // Request was handled as a MASQUE request.
    return;
  }
  QUIC_DLOG(INFO) << "Fetching non-MASQUE response for "
                  << GetRequestHandlerKey(request_handler)
                  << request_headers.DebugString();
  QuicMemoryCacheBackend::FetchResponseFromBackend(
      request_headers, request_body, request_handler);
}

void MasqueServerBackend::CloseBackendResponseStream(
    QuicSimpleServerBackend::RequestHandler* request_handler) {
  QUIC_DLOG(INFO) << "Closing response stream for "
                  << GetRequestHandlerKey(request_handler);
  active_response_map_.erase(GetRequestHandlerKey(request_handler));
  QuicMemoryCacheBackend::CloseBackendResponseStream(request_handler);
}

void MasqueServerBackend::RegisterBackendClient(QuicConnectionId connection_id,
                                                BackendClient* backend_client) {
  QUIC_BUG_IF(backend_clients_.find(connection_id) != backend_clients_.end())
      << connection_id << " already in backend clients map";
  backend_clients_[connection_id] = backend_client;
  QUIC_DLOG(INFO) << "Registering backend client for " << connection_id;
}

void MasqueServerBackend::RemoveBackendClient(QuicConnectionId connection_id) {
  backend_clients_.erase(connection_id);
  QUIC_DLOG(INFO) << "Removing backend client for " << connection_id;
}

}  // namespace quic
