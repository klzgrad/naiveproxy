// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_BASE_NETWORK_CHANGE_NOTIFIER_CHROMEOS_H_
#define NET_BASE_NETWORK_CHANGE_NOTIFIER_CHROMEOS_H_

#include "base/gtest_prod_util.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread.h"
#include "base/threading/thread_checker.h"
#include "net/base/net_export.h"
#include "net/base/network_change_notifier.h"

namespace net {

// A NetworkChangeNotifier that needs to be told about network changes by some
// other object. This class can't directly listen for network changes because
// on ChromeOS only objects running in the browser process can listen for
// network state changes.
class NET_EXPORT NetworkChangeNotifierChromeos : public NetworkChangeNotifier {
 public:
  NetworkChangeNotifierChromeos();
  ~NetworkChangeNotifierChromeos() override;

  // These methods are used to notify this object that a network property has
  // changed. These must be called from the thread that owns this object.
  void OnDNSChanged();
  void OnIPAddressChanged();
  void OnConnectionChanged(
      NetworkChangeNotifier::ConnectionType connection_type);
  void OnConnectionSubtypeChanged(
      NetworkChangeNotifier::ConnectionType connection_type,
      NetworkChangeNotifier::ConnectionSubtype connection_subtype);

 protected:
  // NetworkChangeNotifier overrides.
  NetworkChangeNotifier::ConnectionType GetCurrentConnectionType()
      const override;
  void GetCurrentMaxBandwidthAndConnectionType(
      double* max_bandwidth_mbps,
      ConnectionType* connection_type) const override;

 private:
  FRIEND_TEST_ALL_PREFIXES(NetworkChangeNotifierChromeosTest,
                           ConnectionTypeFromShill);
  friend class NetworkChangeNotifierChromeosTest;

  class DnsConfigService;

  // Thread on which we can run DnsConfigService, which requires a TYPE_IO
  // message loop.
  class NotifierThread : public base::Thread {
   public:
    NotifierThread();
    ~NotifierThread() override;

    void OnNetworkChange();

   protected:
    // base::Thread
    void Init() override;
    void CleanUp() override;

   private:
    std::unique_ptr<DnsConfigService> dns_config_service_;
    SEQUENCE_CHECKER(sequence_checker_);

    DISALLOW_COPY_AND_ASSIGN(NotifierThread);
  };

  // Calculates parameters used for network change notifier online/offline
  // signals.
  static NetworkChangeNotifier::NetworkChangeCalculatorParams
  NetworkChangeCalculatorParamsChromeos();

  THREAD_CHECKER(thread_checker_);

  mutable base::Lock lock_;
  NetworkChangeNotifier::ConnectionType
      connection_type_;        // Guarded by |lock_|.
  double max_bandwidth_mbps_;  // Guarded by |lock_|.

  NotifierThread notifier_thread_;

  DISALLOW_COPY_AND_ASSIGN(NetworkChangeNotifierChromeos);
};

}  // namespace net

#endif  // NET_BASE_NETWORK_CHANGE_NOTIFIER_CHROMEOS_H_
