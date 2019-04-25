// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/bind.h"
#include "build/build_config.h"
#include "net/base/network_change_notifier_posix.h"
#include "net/dns/dns_config_service_posix.h"

#if defined(OS_ANDROID)
#include "net/android/network_change_notifier_android.h"
#endif

namespace net {

// DNS config services on Chrome OS and Android are signalled by the network
// state handler rather than relying on watching files in /etc.
class NetworkChangeNotifierPosix::DnsConfigService
    : public net::internal::DnsConfigServicePosix {
 public:
  DnsConfigService() = default;
  ~DnsConfigService() override = default;

  // net::internal::DnsConfigService() overrides.
  bool StartWatching() override {
    // DNS config changes are handled and notified by the network
    // state handlers.
    return true;
  }

  void OnNetworkChange() {
    InvalidateConfig();
    InvalidateHosts();
    ReadNow();
  }
};

NetworkChangeNotifierPosix::NotifierThread::NotifierThread()
    : base::Thread("NetworkChangeNotifier") {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

NetworkChangeNotifierPosix::NotifierThread::~NotifierThread() {
  DCHECK(!Thread::IsRunning());
}

void NetworkChangeNotifierPosix::NotifierThread::OnNetworkChange() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  dns_config_service_->OnNetworkChange();
}

void NetworkChangeNotifierPosix::NotifierThread::Init() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  dns_config_service_.reset(new DnsConfigService());
  dns_config_service_->WatchConfig(
      base::BindRepeating(&NetworkChangeNotifier::SetDnsConfig));
  dns_config_service_->OnNetworkChange();
}

void NetworkChangeNotifierPosix::NotifierThread::CleanUp() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  dns_config_service_.reset();
}

NetworkChangeNotifierPosix::NetworkChangeNotifierPosix(
    NetworkChangeNotifier::ConnectionType initial_connection_type,
    NetworkChangeNotifier::ConnectionSubtype initial_connection_subtype)
    : NetworkChangeNotifier(NetworkChangeCalculatorParamsPosix()),
      connection_type_(initial_connection_type),
      max_bandwidth_mbps_(
          NetworkChangeNotifier::GetMaxBandwidthMbpsForConnectionSubtype(
              initial_connection_subtype)) {
  notifier_thread_.StartWithOptions(
      base::Thread::Options(base::MessageLoop::TYPE_IO, 0));
}

NetworkChangeNotifierPosix::~NetworkChangeNotifierPosix() {
  notifier_thread_.Stop();
}

void NetworkChangeNotifierPosix::OnDNSChanged() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  // The Unretained thread pointer is ok here because if the thread gets
  // deleted, the callback won't be called.
  notifier_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &NetworkChangeNotifierPosix::NotifierThread::OnNetworkChange,
          base::Unretained(&notifier_thread_)));
}

void NetworkChangeNotifierPosix::OnIPAddressChanged() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  NetworkChangeNotifier::NotifyObserversOfIPAddressChange();
}

void NetworkChangeNotifierPosix::OnConnectionChanged(
    NetworkChangeNotifier::ConnectionType connection_type) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  {
    base::AutoLock scoped_lock(lock_);
    connection_type_ = connection_type;
  }
  NetworkChangeNotifier::NotifyObserversOfConnectionTypeChange();
}

void NetworkChangeNotifierPosix::OnConnectionSubtypeChanged(
    NetworkChangeNotifier::ConnectionType connection_type,
    NetworkChangeNotifier::ConnectionSubtype connection_subtype) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  double max_bandwidth_mbps =
      GetMaxBandwidthMbpsForConnectionSubtype(connection_subtype);
  {
    base::AutoLock scoped_lock(lock_);
    max_bandwidth_mbps_ = max_bandwidth_mbps;
  }
  NetworkChangeNotifier::NotifyObserversOfMaxBandwidthChange(max_bandwidth_mbps,
                                                             connection_type);
}

NetworkChangeNotifier::ConnectionType
NetworkChangeNotifierPosix::GetCurrentConnectionType() const {
  base::AutoLock scoped_lock(lock_);
  return connection_type_;
}

void NetworkChangeNotifierPosix::GetCurrentMaxBandwidthAndConnectionType(
    double* max_bandwidth_mbps,
    ConnectionType* connection_type) const {
  base::AutoLock scoped_lock(lock_);
  *connection_type = connection_type_;
  *max_bandwidth_mbps = max_bandwidth_mbps_;
}

// static
NetworkChangeNotifier::NetworkChangeCalculatorParams
NetworkChangeNotifierPosix::NetworkChangeCalculatorParamsPosix() {
  NetworkChangeCalculatorParams params;
#if defined(OS_CHROMEOS)
  // Delay values arrived at by simple experimentation and adjusted so as to
  // produce a single signal when switching between network connections.
  params.ip_address_offline_delay_ = base::TimeDelta::FromMilliseconds(4000);
  params.ip_address_online_delay_ = base::TimeDelta::FromMilliseconds(1000);
  params.connection_type_offline_delay_ =
      base::TimeDelta::FromMilliseconds(500);
  params.connection_type_online_delay_ = base::TimeDelta::FromMilliseconds(500);
#elif defined(OS_ANDROID)
  params =
      net::NetworkChangeNotifierAndroid::NetworkChangeCalculatorParamsAndroid();
#else
  NOTIMPLEMENTED();
#endif
  return params;
}

}  // namespace net
