// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/test/spawned_test_server/remote_test_server_proxy.h"

#include <memory>
#include <vector>

#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread_checker.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/socket/stream_socket.h"
#include "net/socket/tcp_client_socket.h"
#include "net/socket/tcp_server_socket.h"

namespace net {

namespace {

const int kBufferSize = 1024;

// Helper that reads data from one socket and then forwards to another socket.
class SocketDataPump {
 public:
  SocketDataPump(StreamSocket* from_socket,
                 StreamSocket* to_socket,
                 base::OnceClosure on_done_callback)
      : from_socket_(from_socket),
        to_socket_(to_socket),
        on_done_callback_(std::move(on_done_callback)) {
    read_buffer_ = new IOBuffer(kBufferSize);
  }

  ~SocketDataPump() { DCHECK_CALLED_ON_VALID_THREAD(thread_checker_); }

  void Start() { Read(); }

 private:
  void Read() {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    DCHECK(!write_buffer_);

    int result = from_socket_->Read(
        read_buffer_.get(), kBufferSize,
        base::Bind(&SocketDataPump::HandleReadResult, base::Unretained(this)));
    if (result != ERR_IO_PENDING)
      HandleReadResult(result);
  }

  void HandleReadResult(int result) {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

    if (result <= 0) {
      std::move(on_done_callback_).Run();
      return;
    }

    write_buffer_ = new DrainableIOBuffer(read_buffer_.get(), result);
    Write();
  }

  void Write() {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
    DCHECK(write_buffer_);

    int result = to_socket_->Write(
        write_buffer_.get(), write_buffer_->BytesRemaining(),
        base::Bind(&SocketDataPump::HandleWriteResult, base::Unretained(this)));
    if (result != ERR_IO_PENDING)
      HandleWriteResult(result);
  }

  void HandleWriteResult(int result) {
    DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

    if (result <= 0) {
      std::move(on_done_callback_).Run();
      return;
    }

    write_buffer_->DidConsume(result);
    if (write_buffer_->BytesRemaining()) {
      Write();
    } else {
      write_buffer_ = nullptr;
      Read();
    }
  }

  StreamSocket* from_socket_;
  StreamSocket* to_socket_;

  scoped_refptr<IOBuffer> read_buffer_;
  scoped_refptr<DrainableIOBuffer> write_buffer_;

  base::OnceClosure on_done_callback_;

  THREAD_CHECKER(thread_checker_);

  DISALLOW_COPY_AND_ASSIGN(SocketDataPump);
};

// ConnectionProxy is responsible for proxying one connection to a remote
// address.
class ConnectionProxy {
 public:
  explicit ConnectionProxy(std::unique_ptr<StreamSocket> local_socket);
  ~ConnectionProxy();

  void Start(const IPEndPoint& remote_address,
             base::OnceClosure on_done_callback);

 private:
  void Close();

  void HandleConnectResult(const IPEndPoint& remote_address, int result);

  base::OnceClosure on_done_callback_;

  std::unique_ptr<StreamSocket> local_socket_;
  std::unique_ptr<StreamSocket> remote_socket_;

  std::unique_ptr<SocketDataPump> incoming_pump_;
  std::unique_ptr<SocketDataPump> outgoing_pump_;

  THREAD_CHECKER(thread_checker_);

  base::WeakPtrFactory<ConnectionProxy> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ConnectionProxy);
};

ConnectionProxy::ConnectionProxy(std::unique_ptr<StreamSocket> local_socket)
    : local_socket_(std::move(local_socket)), weak_factory_(this) {}

ConnectionProxy::~ConnectionProxy() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
}

void ConnectionProxy::Start(const IPEndPoint& remote_address,
                            base::OnceClosure on_done_callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  on_done_callback_ = std::move(on_done_callback);
  remote_socket_ = std::make_unique<TCPClientSocket>(
      AddressList(remote_address), nullptr, nullptr, NetLogSource());
  int result = remote_socket_->Connect(
      base::Bind(&ConnectionProxy::HandleConnectResult, base::Unretained(this),
                 remote_address));
  if (result != ERR_IO_PENDING)
    HandleConnectResult(remote_address, result);
}

void ConnectionProxy::HandleConnectResult(const IPEndPoint& remote_address,
                                          int result) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(!incoming_pump_);
  DCHECK(!outgoing_pump_);

  if (result < 0) {
    LOG(ERROR) << "Connection to " << remote_address.ToString()
               << " failed: " << ErrorToString(result);
    Close();
    return;
  }

  incoming_pump_ = std::make_unique<SocketDataPump>(
      remote_socket_.get(), local_socket_.get(),
      base::BindOnce(&ConnectionProxy::Close, base::Unretained(this)));
  outgoing_pump_ = std::make_unique<SocketDataPump>(
      local_socket_.get(), remote_socket_.get(),
      base::BindOnce(&ConnectionProxy::Close, base::Unretained(this)));

  auto self = weak_factory_.GetWeakPtr();
  incoming_pump_->Start();
  if (!self)
    return;

  outgoing_pump_->Start();
}

void ConnectionProxy::Close() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  local_socket_.reset();
  remote_socket_.reset();
  std::move(on_done_callback_).Run();
}

}  // namespace

// RemoteTestServerProxy implementation that runs on a background IO thread.
class RemoteTestServerProxy::Core {
 public:
  explicit Core(const IPEndPoint& remote_address);
  ~Core();

  void Start(base::WaitableEvent* started_event);

  uint16_t local_port() const { return local_port_; }

 private:
  void DoAcceptLoop();
  void OnAcceptResult(int result);
  void HandleAcceptResult(int result);
  void OnConnectionClosed(ConnectionProxy* connection);

  IPEndPoint remote_address_;

  std::unique_ptr<TCPServerSocket> socket_;

  uint16_t local_port_;
  std::vector<std::unique_ptr<ConnectionProxy>> connections_;

  std::unique_ptr<StreamSocket> accepted_socket_;

  DISALLOW_COPY_AND_ASSIGN(Core);
};

RemoteTestServerProxy::Core::Core(const IPEndPoint& remote_address)
    : remote_address_(remote_address) {}

void RemoteTestServerProxy::Core::Start(base::WaitableEvent* started_event) {
  socket_ = std::make_unique<TCPServerSocket>(nullptr, net::NetLogSource());
  int result = socket_->Listen(IPEndPoint(IPAddress::IPv4Localhost(), 0), 5);
  CHECK_EQ(result, OK);

  // Get local port number.
  IPEndPoint address;
  result = socket_->GetLocalAddress(&address);
  CHECK_EQ(result, OK);
  local_port_ = address.port();

  DoAcceptLoop();

  started_event->Signal();
}

RemoteTestServerProxy::Core::~Core() {}

void RemoteTestServerProxy::Core::DoAcceptLoop() {
  int result = OK;
  while (result == OK) {
    result = socket_->Accept(
        &accepted_socket_,
        base::Bind(&Core::OnAcceptResult, base::Unretained(this)));
    if (result != ERR_IO_PENDING)
      HandleAcceptResult(result);
  }
}

void RemoteTestServerProxy::Core::OnAcceptResult(int result) {
  HandleAcceptResult(result);
  if (result == OK)
    DoAcceptLoop();
}

void RemoteTestServerProxy::Core::HandleAcceptResult(int result) {
  DCHECK_NE(result, ERR_IO_PENDING);

  if (result < 0) {
    LOG(ERROR) << "Error when accepting a connection: "
               << ErrorToString(result);
    return;
  }

  std::unique_ptr<ConnectionProxy> connection_proxy =
      std::make_unique<ConnectionProxy>(std::move(accepted_socket_));
  ConnectionProxy* connection_proxy_ptr = connection_proxy.get();
  connections_.push_back(std::move(connection_proxy));

  // Start() may invoke the callback so it needs to be called after the
  // connection is pushed to connections_.
  connection_proxy_ptr->Start(
      remote_address_,
      base::BindOnce(&Core::OnConnectionClosed, base::Unretained(this),
                     connection_proxy_ptr));
}

void RemoteTestServerProxy::Core::OnConnectionClosed(
    ConnectionProxy* connection) {
  for (auto it = connections_.begin(); it != connections_.end(); ++it) {
    if (it->get() == connection) {
      connections_.erase(it);
      return;
    }
  }
  NOTREACHED();
}

RemoteTestServerProxy::RemoteTestServerProxy(
    const IPEndPoint& remote_address,
    scoped_refptr<base::SingleThreadTaskRunner> io_task_runner)
    : io_task_runner_(io_task_runner),
      core_(std::make_unique<Core>(remote_address)) {
  base::WaitableEvent started_event(
      base::WaitableEvent::ResetPolicy::MANUAL,
      base::WaitableEvent::InitialState::NOT_SIGNALED);
  io_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&Core::Start, base::Unretained(core_.get()), &started_event));
  started_event.Wait();

  local_port_ = core_->local_port();
}

RemoteTestServerProxy::~RemoteTestServerProxy() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  io_task_runner_->DeleteSoon(FROM_HERE, core_.release());
}

}  // namespace net
