// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NETWORK_CHANGE_NOTIFIER_WIN_H_
#define NET_BASE_NETWORK_CHANGE_NOTIFIER_WIN_H_

#include <windows.h>

#include <atomic>

#include "base/compiler_specific.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/thread_annotations.h"
#include "base/threading/sequence_bound.h"
#include "base/timer/timer.h"
#include "base/win/object_watcher.h"
#include "net/base/net_export.h"
#include "net/base/network_change_notifier.h"

namespace base {
class SequencedTaskRunner;
}  // namespace base

namespace net {

class NetworkCostChangeNotifierWin;

// NetworkChangeNotifierWin uses a SequenceChecker, as all its internal
// notification code must be called on the sequence it is created and destroyed
// on.  All the NetworkChangeNotifier methods it implements are threadsafe.
class NET_EXPORT_PRIVATE NetworkChangeNotifierWin
    : public NetworkChangeNotifier,
      public base::win::ObjectWatcher::Delegate {
 public:
  NetworkChangeNotifierWin();
  NetworkChangeNotifierWin(const NetworkChangeNotifierWin&) = delete;
  NetworkChangeNotifierWin& operator=(const NetworkChangeNotifierWin&) = delete;
  ~NetworkChangeNotifierWin() override;

  // Begins listening for a single subsequent address change.  If it fails to
  // start watching, it retries on a timer.  Must be called only once, on the
  // sequence |this| was created on.  This cannot be called in the constructor,
  // as WatchForAddressChangeInternal is mocked out in unit tests.
  // TODO(mmenke): Consider making this function a part of the
  //               NetworkChangeNotifier interface, so other subclasses can be
  //               unit tested in similar fashion, as needed.
  void WatchForAddressChange();

 protected:
  // For unit tests only.
  bool is_watching() const { return is_watching_; }
  void set_is_watching(bool is_watching) { is_watching_ = is_watching; }
  int sequential_failures() const { return sequential_failures_; }

 private:
  friend class NetworkChangeNotifierWinTest;
  friend class TestNetworkChangeNotifierWin;

  // NetworkChangeNotifier methods:
  ConnectionCost GetCurrentConnectionCost() override;

  ConnectionType GetCurrentConnectionType() const override;

  // ObjectWatcher::Delegate methods:
  // Must only be called on the sequence |this| was created on.
  void OnObjectSignaled(HANDLE object) override;

  // Recompute the current connection type on newer versions of Windows (Win10
  // Build 19041 and above).
  static ConnectionType RecomputeCurrentConnectionTypeModern();

  // Does the actual work to determine the current connection type. This will
  // call into RecomputeCurrentConnectionTypeModern on modern OS. It is not
  // thread safe, see crbug.com/324913.
  static ConnectionType RecomputeCurrentConnectionType();

  // Calls RecomputeCurrentConnectionTypeImpl on the DNS sequence and runs
  // |reply_callback| with the type on the calling sequence.
  virtual void RecomputeCurrentConnectionTypeOnBlockingSequence(
      base::OnceCallback<void(ConnectionType)> reply_callback) const;

  void SetCurrentConnectionType(ConnectionType connection_type);

  // Notifies IP address change observers of a change immediately, and notifies
  // network state change observers on a delay.  Must only be called on the
  // sequence |this| was created on.
  void NotifyObservers(ConnectionType connection_type);

  // Forwards connection type notifications to parent class.
  void NotifyParentOfConnectionTypeChange();
  void NotifyParentOfConnectionTypeChangeImpl(ConnectionType connection_type);

  // Tries to start listening for a single subsequent address change.  Returns
  // false on failure.  The caller is responsible for updating |is_watching_|.
  // Virtual for unit tests.  Must only be called on the sequence |this| was
  // created on.
  virtual bool WatchForAddressChangeInternal();

  static NetworkChangeCalculatorParams NetworkChangeCalculatorParamsWin();

  void OnCostChanged(NetworkChangeNotifier::ConnectionCost new_cost);

  // All member variables may only be accessed on the sequence |this| was
  // created on.

  // False when not currently watching for network change events.  This only
  // happens on initialization and when WatchForAddressChangeInternal fails and
  // there is a pending task to try again.  Needed for safe cleanup.
  bool is_watching_ = false;

  base::win::ObjectWatcher addr_watcher_;
  OVERLAPPED addr_overlapped_;

  base::OneShotTimer timer_;

  // Number of times WatchForAddressChange has failed in a row.
  int sequential_failures_ = 0;

  scoped_refptr<base::SequencedTaskRunner> blocking_task_runner_;

  mutable base::Lock last_computed_connection_type_lock_;
  ConnectionType last_computed_connection_type_;

  std::atomic<NetworkChangeNotifier::ConnectionCost>
      last_computed_connection_cost_ =
          NetworkChangeNotifier::ConnectionCost::CONNECTION_COST_UNKNOWN;

  // Provides the cost of the current connection.  Uses the Windows OS APIs to
  // monitor and determine cost.
  base::SequenceBound<NetworkCostChangeNotifierWin> cost_change_notifier_;

  // Result of IsOffline() when NotifyObserversOfConnectionTypeChange()
  // was last called.
  bool last_announced_offline_;
  // Number of times polled to check if still offline.
  int offline_polls_;

  // Used to ensure that all registration actions are properly sequenced on the
  // same thread regardless of which thread was used to call into the
  // NetworkChangeNotifier API.
  scoped_refptr<base::SequencedTaskRunner> sequence_runner_for_registration_;

  SEQUENCE_CHECKER(sequence_checker_);

  // Used for calling WatchForAddressChange again on failure.
  base::WeakPtrFactory<NetworkChangeNotifierWin> weak_factory_{this};
};

}  // namespace net

#endif  // NET_BASE_NETWORK_CHANGE_NOTIFIER_WIN_H_
