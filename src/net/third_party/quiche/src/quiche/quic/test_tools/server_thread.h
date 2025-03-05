// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TEST_TOOLS_SERVER_THREAD_H_
#define QUICHE_QUIC_TEST_TOOLS_SERVER_THREAD_H_

#include <memory>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "quiche/quic/core/quic_config.h"
#include "quiche/quic/platform/api/quic_socket_address.h"
#include "quiche/quic/platform/api/quic_thread.h"
#include "quiche/quic/tools/quic_server.h"
#include "quiche/common/quiche_callbacks.h"

namespace quic {
namespace test {

// Simple wrapper class to run QuicServer in a dedicated thread.
class ServerThread : public QuicThread {
 public:
  ServerThread(std::unique_ptr<QuicServer> server,
               const QuicSocketAddress& address);
  ServerThread(const ServerThread&) = delete;
  ServerThread& operator=(const ServerThread&) = delete;

  ~ServerThread() override;

  // Prepares the server, but does not start accepting connections. Useful for
  // injecting mocks.
  void Initialize();

  // Runs the event loop. Will initialize if necessary.
  void Run() override;

  // Schedules the given action for execution in the event loop.
  void Schedule(quiche::SingleUseCallback<void()> action);

  // Like |Schedule|, but wait for |action| to complete before function returns.
  void ScheduleAndWaitForCompletion(quiche::SingleUseCallback<void()> action);

  // Waits for the handshake to be confirmed for the first session created.
  void WaitForCryptoHandshakeConfirmed();

  // Wait until |termination_predicate| returns true in server thread, or
  // reached |timeout|. Must be called from an external thread.
  // Return whether the function returned after |termination_predicate| become
  // true.
  bool WaitUntil(quiche::UnretainedCallback<bool()> termination_predicate,
                 QuicTime::Delta timeout);

  // Pauses execution of the server until Resume() is called.  May only be
  // called once.
  void Pause();

  // Resumes execution of the server after Pause() has been called.  May only
  // be called once.
  void Resume();

  // Stops the server from executing and shuts it down, destroying all
  // server objects.
  void Quit();

  // Returns the underlying server.  Care must be taken to avoid data races
  // when accessing the server.  It is always safe to access the server
  // after calling Pause() and before calling Resume().
  QuicServer* server() { return server_.get(); }

  // Returns the port that the server is listening on.
  int GetPort();

 private:
  void MaybeNotifyOfHandshakeConfirmation();
  void ExecuteScheduledActions();

  absl::Notification
      confirmed_;             // Notified when the first handshake is confirmed.
  absl::Notification pause_;  // Notified when the server should pause.
  absl::Notification paused_;  // Notitied when the server has paused
  absl::Notification resume_;  // Notified when the server should resume.
  absl::Notification quit_;    // Notified when the server should quit.

  std::unique_ptr<QuicServer> server_;
  QuicClock* clock_;
  QuicSocketAddress address_;
  mutable absl::Mutex port_lock_;
  int port_ ABSL_GUARDED_BY(port_lock_);

  bool initialized_;

  absl::Mutex scheduled_actions_lock_;
  quiche::QuicheCircularDeque<quiche::SingleUseCallback<void()>>
      scheduled_actions_ ABSL_GUARDED_BY(scheduled_actions_lock_);
};

}  // namespace test
}  // namespace quic

#endif  // QUICHE_QUIC_TEST_TOOLS_SERVER_THREAD_H_
