// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/quic/test_tools/server_thread.h"

#include <memory>
#include <utility>

#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "quiche/quic/core/quic_default_clock.h"
#include "quiche/quic/core/quic_dispatcher.h"
#include "quiche/quic/test_tools/crypto_test_utils.h"
#include "quiche/quic/test_tools/quic_dispatcher_peer.h"
#include "quiche/quic/test_tools/quic_server_peer.h"
#include "quiche/common/quiche_callbacks.h"

namespace quic {
namespace test {

ServerThread::ServerThread(std::unique_ptr<QuicServer> server,
                           const QuicSocketAddress& address)
    : QuicThread("server_thread"),
      server_(std::move(server)),
      clock_(QuicDefaultClock::Get()),
      address_(address),
      port_(0),
      initialized_(false) {}

ServerThread::~ServerThread() = default;

void ServerThread::Initialize() {
  if (initialized_) {
    return;
  }
  if (!server_->CreateUDPSocketAndListen(address_)) {
    return;
  }

  absl::WriterMutexLock lock(&port_lock_);
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
  absl::ReaderMutexLock lock(&port_lock_);
  int rc = port_;
  return rc;
}

void ServerThread::Schedule(quiche::SingleUseCallback<void()> action) {
  QUICHE_DCHECK(!quit_.HasBeenNotified());
  absl::WriterMutexLock lock(&scheduled_actions_lock_);
  scheduled_actions_.push_back(std::move(action));
}

void ServerThread::ScheduleAndWaitForCompletion(
    quiche::SingleUseCallback<void()> action) {
  absl::Notification action_done;
  Schedule([&] {
    std::move(action)();
    action_done.Notify();
  });
  action_done.WaitForNotification();
}

void ServerThread::WaitForCryptoHandshakeConfirmed() {
  confirmed_.WaitForNotification();
}

bool ServerThread::WaitUntil(
    quiche::UnretainedCallback<bool()> termination_predicate,
    QuicTime::Delta timeout) {
  const QuicTime deadline = clock_->Now() + timeout;
  while (clock_->Now() < deadline) {
    absl::Notification done_checking;
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
  QUICHE_DCHECK(!pause_.HasBeenNotified());
  pause_.Notify();
  paused_.WaitForNotification();
}

void ServerThread::Resume() {
  QUICHE_DCHECK(!resume_.HasBeenNotified());
  QUICHE_DCHECK(pause_.HasBeenNotified());
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
  if (dispatcher->NumSessions() == 0) {
    // Wait for a session to be created.
    return;
  }
  QuicSession* session = QuicDispatcherPeer::GetFirstSessionIfAny(dispatcher);
  if (session->OneRttKeysAvailable()) {
    confirmed_.Notify();
  }
}

void ServerThread::ExecuteScheduledActions() {
  quiche::QuicheCircularDeque<quiche::SingleUseCallback<void()>> actions;
  {
    absl::WriterMutexLock lock(&scheduled_actions_lock_);
    actions.swap(scheduled_actions_);
  }
  while (!actions.empty()) {
    std::move(actions.front())();
    actions.pop_front();
  }
}

}  // namespace test
}  // namespace quic
