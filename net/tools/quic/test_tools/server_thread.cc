// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/test_tools/server_thread.h"

#include "net/quic/platform/api/quic_containers.h"
#include "net/quic/test_tools/crypto_test_utils.h"
#include "net/tools/quic/quic_dispatcher.h"
#include "net/tools/quic/test_tools/quic_server_peer.h"

namespace net {
namespace test {

ServerThread::ServerThread(QuicServer* server, const QuicSocketAddress& address)
    : SimpleThread("server_thread"),
      confirmed_(base::WaitableEvent::ResetPolicy::MANUAL,
                 base::WaitableEvent::InitialState::NOT_SIGNALED),
      pause_(base::WaitableEvent::ResetPolicy::MANUAL,
             base::WaitableEvent::InitialState::NOT_SIGNALED),
      paused_(base::WaitableEvent::ResetPolicy::MANUAL,
              base::WaitableEvent::InitialState::NOT_SIGNALED),
      resume_(base::WaitableEvent::ResetPolicy::MANUAL,
              base::WaitableEvent::InitialState::NOT_SIGNALED),
      quit_(base::WaitableEvent::ResetPolicy::MANUAL,
            base::WaitableEvent::InitialState::NOT_SIGNALED),
      server_(server),
      address_(address),
      port_(0),
      initialized_(false) {}

ServerThread::~ServerThread() = default;

void ServerThread::Initialize() {
  if (initialized_) {
    return;
  }

  server_->CreateUDPSocketAndListen(address_);

  QuicWriterMutexLock lock(&port_lock_);
  port_ = server_->port();

  initialized_ = true;
}

void ServerThread::Run() {
  if (!initialized_) {
    Initialize();
  }

  while (!quit_.IsSignaled()) {
    if (pause_.IsSignaled() && !resume_.IsSignaled()) {
      paused_.Signal();
      resume_.Wait();
    }
    server_->WaitForEvents();
    ExecuteScheduledActions();
    MaybeNotifyOfHandshakeConfirmation();
  }

  server_->Shutdown();
}

int ServerThread::GetPort() {
  QuicReaderMutexLock lock(&port_lock_);
  int rc = port_;
  return rc;
}

void ServerThread::Schedule(std::function<void()> action) {
  DCHECK(!quit_.IsSignaled());
  QuicWriterMutexLock lock(&scheduled_actions_lock_);
  scheduled_actions_.push_back(std::move(action));
}

void ServerThread::WaitForCryptoHandshakeConfirmed() {
  confirmed_.Wait();
}

void ServerThread::Pause() {
  DCHECK(!pause_.IsSignaled());
  pause_.Signal();
  paused_.Wait();
}

void ServerThread::Resume() {
  DCHECK(!resume_.IsSignaled());
  DCHECK(pause_.IsSignaled());
  resume_.Signal();
}

void ServerThread::Quit() {
  if (pause_.IsSignaled() && !resume_.IsSignaled()) {
    resume_.Signal();
  }
  quit_.Signal();
}

void ServerThread::MaybeNotifyOfHandshakeConfirmation() {
  if (confirmed_.IsSignaled()) {
    // Only notify once.
    return;
  }
  QuicDispatcher* dispatcher = QuicServerPeer::GetDispatcher(server());
  if (dispatcher->session_map().empty()) {
    // Wait for a session to be created.
    return;
  }
  QuicSession* session = dispatcher->session_map().begin()->second.get();
  if (session->IsCryptoHandshakeConfirmed()) {
    confirmed_.Signal();
  }
}

void ServerThread::ExecuteScheduledActions() {
  QuicDeque<std::function<void()>> actions;
  {
    QuicWriterMutexLock lock(&scheduled_actions_lock_);
    actions.swap(scheduled_actions_);
  }
  while (!actions.empty()) {
    actions.front()();
    actions.pop_front();
  }
}

}  // namespace test
}  // namespace net
