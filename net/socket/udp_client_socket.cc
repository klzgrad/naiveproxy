// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/udp_client_socket.h"

#include "net/base/net_errors.h"

namespace net {

UDPClientSocket::UDPClientSocket(DatagramSocket::BindType bind_type,
                                 const RandIntCallback& rand_int_cb,
                                 net::NetLog* net_log,
                                 const net::NetLogSource& source)
    : socket_(bind_type, rand_int_cb, net_log, source),
      network_(NetworkChangeNotifier::kInvalidNetworkHandle) {}

UDPClientSocket::~UDPClientSocket() {
}

int UDPClientSocket::Connect(const IPEndPoint& address) {
  int rv = socket_.Open(address.GetFamily());
  if (rv != OK)
    return rv;
  return socket_.Connect(address);
}

int UDPClientSocket::ConnectUsingNetwork(
    NetworkChangeNotifier::NetworkHandle network,
    const IPEndPoint& address) {
  if (!NetworkChangeNotifier::AreNetworkHandlesSupported())
    return ERR_NOT_IMPLEMENTED;
  int rv = socket_.Open(address.GetFamily());
  if (rv != OK)
    return rv;
  rv = socket_.BindToNetwork(network);
  if (rv != OK)
    return rv;
  network_ = network;
  return socket_.Connect(address);
}

int UDPClientSocket::ConnectUsingDefaultNetwork(const IPEndPoint& address) {
  if (!NetworkChangeNotifier::AreNetworkHandlesSupported())
    return ERR_NOT_IMPLEMENTED;
  int rv;
  rv = socket_.Open(address.GetFamily());
  if (rv != OK)
    return rv;
  // Calling connect() will bind a socket to the default network, however there
  // is no way to determine what network the socket got bound to.  The
  // alternative is to query what the default network is and bind the socket to
  // that network explicitly, however this is racy because the default network
  // can change in between when we query it and when we bind to it.  This is
  // rare but should be accounted for.  Since changes of the default network
  // should not come in quick succession, we can simply try again.
  NetworkChangeNotifier::NetworkHandle network;
  for (int attempt = 0; attempt < 2; attempt++) {
    network = NetworkChangeNotifier::GetDefaultNetwork();
    if (network == NetworkChangeNotifier::kInvalidNetworkHandle)
      return ERR_INTERNET_DISCONNECTED;
    rv = socket_.BindToNetwork(network);
    // |network| may have disconnected between the call to GetDefaultNetwork()
    // and the call to BindToNetwork(). Loop only if this is the case (|rv| will
    // be ERR_NETWORK_CHANGED).
    if (rv != ERR_NETWORK_CHANGED)
      break;
  }
  if (rv != OK)
    return rv;
  network_ = network;
  return socket_.Connect(address);
}

NetworkChangeNotifier::NetworkHandle UDPClientSocket::GetBoundNetwork() const {
  return network_;
}

int UDPClientSocket::Read(IOBuffer* buf,
                          int buf_len,
                          const CompletionCallback& callback) {
  return socket_.Read(buf, buf_len, callback);
}

int UDPClientSocket::Write(IOBuffer* buf,
                          int buf_len,
                          const CompletionCallback& callback) {
  return socket_.Write(buf, buf_len, callback);
}

void UDPClientSocket::Close() {
  socket_.Close();
}

int UDPClientSocket::GetPeerAddress(IPEndPoint* address) const {
  return socket_.GetPeerAddress(address);
}

int UDPClientSocket::GetLocalAddress(IPEndPoint* address) const {
  return socket_.GetLocalAddress(address);
}

int UDPClientSocket::SetReceiveBufferSize(int32_t size) {
  return socket_.SetReceiveBufferSize(size);
}

int UDPClientSocket::SetSendBufferSize(int32_t size) {
  return socket_.SetSendBufferSize(size);
}

int UDPClientSocket::SetDoNotFragment() {
  return socket_.SetDoNotFragment();
}

const NetLogWithSource& UDPClientSocket::NetLog() const {
  return socket_.NetLog();
}

void UDPClientSocket::UseNonBlockingIO() {
#if defined(OS_WIN)
  socket_.UseNonBlockingIO();
#endif
}

}  // namespace net
