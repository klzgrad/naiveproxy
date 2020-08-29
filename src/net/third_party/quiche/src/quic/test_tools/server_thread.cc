// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/test_tools/server_thread.h"

#include "net/third_party/quiche/src/quic/core/quic_dispatcher.h"
#include "net/third_party/quiche/src/quic/platform/api/quic_containers.h"
#include "net/third_party/quiche/src/quic/test_tools/crypto_test_utils.h"
#include "net/third_party/quiche/src/quic/test_tools/quic_server_peer.h"

namespace quic {
namespace test {

ServerThread::ServerThread(QuicServer* server, const QuicSocketAddress& address)
    : QuicThread("server_thread"),
      server_(server),
      clock_(server->epoll_server()),
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

  while (!quit_.HasBeenNotified()) {
    if (pause_.HasBeenNotified() && !resume_.HasBeenNotified()) {
      paused_.Notify();
      resume_.WaitForNotification();
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
  DCHECK(!quit_.HasBeenNotified());
  QuicWriterMutexLock lock(&scheduled_actions_lock_);
  scheduled_actions_.push_back(std::move(action));
}

void ServerThread::WaitForCryptoHandshakeConfirmed() {
  confirmed_.WaitForNotification();
}

bool ServerThread::WaitUntil(std::function<bool()> termination_predicate,
                             QuicTime::Delta timeout) {
  const QuicTime deadline = clock_.Now() + timeout;
  while (clock_.Now() < deadline) {
    QuicNotification done_checking;
    bool should_terminate = false;
    Schedule([&] {
      should_terminate = termination_predicate();
      done_checking.Notify();
    });
    done_checking.WaitForNotification();
    if (should_terminate) {
      return true;
    }
  }
  return false;
}

void ServerThread::Pause() {
  DCHECK(!pause_.HasBeenNotified());
  pause_.Notify();
  paused_.WaitForNotification();
}

void ServerThread::Resume() {
  DCHECK(!resume_.HasBeenNotified());
  DCHECK(pause_.HasBeenNotified());
  resume_.Notify();
}

void ServerThread::Quit() {
  if (pause_.HasBeenNotified() && !resume_.HasBeenNotified()) {
    resume_.Notify();
  }
  if (!quit_.HasBeenNotified()) {
    quit_.Notify();
  }
}

void ServerThread::MaybeNotifyOfHandshakeConfirmation() {
  if (confirmed_.HasBeenNotified()) {
    // Only notify once.
    return;
  }
  QuicDispatcher* dispatcher = QuicServerPeer::GetDispatcher(server());
  if (dispatcher->session_map().empty()) {
    // Wait for a session to be created.
    return;
  }
  QuicSession* session = dispatcher->session_map().begin()->second.get();
  if (session->OneRttKeysAvailable()) {
    confirmed_.Notify();
  }
}

void ServerThread::ExecuteScheduledActions() {
  QuicCircularDeque<std::function<void()>> actions;
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
}  // namespace quic
