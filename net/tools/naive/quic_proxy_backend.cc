// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/logging.h"
#include "net/third_party/quic/core/http/quic_header_list.h"
#include "net/third_party/quic/core/http/quic_spdy_stream.h"
#include "net/third_party/spdy/core/spdy_header_block.h"
#include "net/tools/naive/quic_proxy_backend.h"

namespace net {

QuicProxyBackend::QuicProxyBackend(
    HttpNetworkSession* session,
    const NetworkTrafficAnnotationTag& traffic_annotation)
    : session_(session), traffic_annotation_(traffic_annotation) {}

QuicProxyBackend::~QuicProxyBackend() {}

bool QuicProxyBackend::InitializeBackend(const std::string& backend_url) {
  return true;
}

bool QuicProxyBackend::IsBackendInitialized() const {
  return true;
}

void QuicProxyBackend::FetchResponseFromBackend(
    const spdy::SpdyHeaderBlock& request_headers,
    const std::string& incoming_body,
    QuicSimpleServerBackend::RequestHandler* quic_stream) {}

void QuicProxyBackend::CloseBackendResponseStream(
    QuicSimpleServerBackend::RequestHandler* quic_stream) {}

void QuicProxyBackend::OnReadHeaders(quic::QuicSpdyStream* stream,
                                     const quic::QuicHeaderList& header_list) {
  HostPortPair request_endpoint;
  for (const auto& p : header_list) {
    const auto& name = p.first;
    const auto& value = p.second;
    if (name == ":method" && value != "CONNECT") {
      spdy::SpdyHeaderBlock headers;
      headers[":status"] = "405";
      stream->WriteHeaders(std::move(headers), /*fin=*/true, nullptr);
      return;
    }
    if (name == ":authority") {
      request_endpoint = HostPortPair::FromString(value);
    }
  }

  if (request_endpoint.IsEmpty()) {
    spdy::SpdyHeaderBlock headers;
    headers[":status"] = "400";
    stream->WriteHeaders(std::move(headers), /*fin=*/true, nullptr);
    LOG(ERROR) << "Invalid origin";
    return;
  }

  
  auto connection_ptr = std::make_unique<QuicConnection>(++last_id_, stream, this, traffic_annotation_);
  auto* connection = connection_ptr.get();
  connection_by_id_[connection->id()] = std::move(connection_ptr);
  int result = connection->Connect(
      base::BindRepeating(&QuicProxyBackend::OnConnectComplete,
                          weak_ptr_factory_.GetWeakPtr(), connection->id()));
  if (result == ERR_IO_PENDING)
    return;
  HandleConnectResult(connection, result);
}

void QuicProxyBackend::OnConnectComplete(int connection_id, int result) {
  auto* connection = FindConnection(connection_id);
  if (!connection)
    return;
  HandleConnectResult(connection, result);
}

void QuicProxyBackend::HandleConnectResult(QuicConnection* connection, int result) {
  if (result != OK) {
    Close(connection->id(), result);
    return;
  }
  DoRun(connection);
}

void QuicProxyBackend::DoRun(QuicConnection* connection) {
  int result = connection->Run(
      base::BindRepeating(&QuicProxyBackend::OnRunComplete,
                          weak_ptr_factory_.GetWeakPtr(), connection->id()));
  if (result == ERR_IO_PENDING)
    return;
  HandleRunResult(connection, result);
}

void QuicProxyBackend::OnRunComplete(int connection_id, int result) {
  auto* connection = FindConnection(connection_id);
  if (!connection)
    return;
  HandleRunResult(connection, result);
}

void QuicProxyBackend::HandleRunResult(QuicConnection* connection, int result) {
  Close(connection->id(), result);
}

void QuicProxyBackend::Close(int connection_id, int reason) {
  auto it = connection_by_id_.find(connection_id);
  if (it == connection_by_id_.end())
    return;

  LOG(INFO) << "Connection " << connection_id
            << " closed: " << ErrorToShortString(reason);

  it->second->Close();
  // The call stack might have callbacks which still have the pointer of
  // connection. Instead of referencing connection with ID all the time,
  // destroys the connection in next run loop to make sure any pending
  // callbacks in the call stack return.
  base::ThreadTaskRunnerHandle::Get()->DeleteSoon(FROM_HERE,
                                                  std::move(it->second));
  connection_by_id_.erase(it);
}

QuicConnection* QuicProxyBackend::FindConnection(int connection_id) {
  auto it = connection_by_id_.find(connection_id);
  if (it == connection_by_id_.end())
    return nullptr;
  return it->second.get();
}


void QuicProxyBackend::OnReadData(quic::QuicSpdyStream* stream,
                                  void* data,
                                  size_t len) {
  LOG(INFO) << "OnReadData " << stream;
}

void QuicProxyBackend::OnCloseStream(quic::QuicSpdyStream* stream) {
  LOG(INFO) << "OnCloseStream " << stream;
}

}  // namespace net
