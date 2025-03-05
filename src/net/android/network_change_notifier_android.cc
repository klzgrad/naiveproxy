// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

////////////////////////////////////////////////////////////////////////////////
// Threading considerations:
//
// This class is designed to meet various threading guarantees starting from the
// ones imposed by NetworkChangeNotifier:
// - The notifier can be constructed on any thread.
// - GetCurrentConnectionType() can be called on any thread.
//
// The fact that this implementation of NetworkChangeNotifier is backed by a
// Java side singleton class (see NetworkChangeNotifier.java) adds another
// threading constraint:
// - The calls to the Java side (stateful) object must be performed from a
//   single thread. This object happens to be a singleton which is used on the
//   application side on the main thread. Therefore all the method calls from
//   the native NetworkChangeNotifierAndroid class to its Java counterpart are
//   performed on the main thread.
//
// This leads to a design involving the following native classes:
// 1) NetworkChangeNotifierFactoryAndroid ('factory')
// 2) NetworkChangeNotifierDelegateAndroid ('delegate')
// 3) NetworkChangeNotifierAndroid ('notifier')
//
// The factory constructs and owns the delegate. The factory is constructed and
// destroyed on the main thread which makes it construct and destroy the
// delegate on the main thread too. This guarantees that the calls to the Java
// side are performed on the main thread.
// Note that after the factory's construction, the factory's creation method can
// be called from any thread since the delegate's construction (performing the
// JNI calls) already happened on the main thread (when the factory was
// constructed).
//
////////////////////////////////////////////////////////////////////////////////
// Propagation of network change notifications:
//
// When the factory is requested to create a new instance of the notifier, the
// factory passes the delegate to the notifier (without transferring ownership).
// Note that there is a one-to-one mapping between the factory and the
// delegate as explained above. But the factory naturally creates multiple
// instances of the notifier. That means that there is a one-to-many mapping
// between delegate and notifier (i.e. a single delegate can be shared by
// multiple notifiers).
// At construction the notifier (which is also an observer) subscribes to
// notifications fired by the delegate. These notifications, received by the
// delegate (and forwarded to the notifier(s)), are sent by the Java side
// notifier (see NetworkChangeNotifier.java) and are initiated by the Android
// platform.
// Notifications from the Java side always arrive on the main thread. The
// delegate then forwards these notifications to the threads of each observer
// (network change notifier). The network change notifier then processes the
// state change, and notifies each of its observers on their threads.
//
// This can also be seen as:
// Android platform -> NetworkChangeNotifier (Java) ->
// NetworkChangeNotifierDelegateAndroid -> NetworkChangeNotifierAndroid.

#include "net/android/network_change_notifier_android.h"

#include <string>
#include <unordered_set>

#include "base/android/build_info.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/metrics/histogram_macros.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/threading/thread.h"
#include "net/base/address_tracker_linux.h"

namespace net {

// Expose handles::kInvalidNetworkHandle out to Java as NetId.INVALID. The
// notion of a NetID is an Android framework one, see android.net.Network.netId.
// NetworkChangeNotifierAndroid implements handles::NetworkHandle to simply be
// the NetID.
// GENERATED_JAVA_ENUM_PACKAGE: org.chromium.net
enum NetId {
  // Cannot use |handles::kInvalidNetworkHandle| here as the Java generator
  // fails, instead enforce their equality with CHECK in
  // NetworkChangeNotifierAndroid().
  INVALID = -1
};

// Thread on which we can run DnsConfigService, which requires a TYPE_IO
// message loop to monitor /system/etc/hosts.
class NetworkChangeNotifierAndroid::BlockingThreadObjects {
 public:
  BlockingThreadObjects()
      : address_tracker_(
            base::DoNothing(),
            base::DoNothing(),
            // We're only interested in tunnel interface changes.
            base::BindRepeating(NotifyNetworkChangeNotifierObservers),
            std::unordered_set<std::string>()) {}
  BlockingThreadObjects(const BlockingThreadObjects&) = delete;
  BlockingThreadObjects& operator=(const BlockingThreadObjects&) = delete;

  void Init() {
    address_tracker_.Init();
  }

  static void NotifyNetworkChangeNotifierObservers() {
    NetworkChangeNotifier::NotifyObserversOfIPAddressChange();
    NetworkChangeNotifier::NotifyObserversOfConnectionTypeChange();
  }

 private:
  // Used to detect tunnel state changes.
  internal::AddressTrackerLinux address_tracker_;
};

NetworkChangeNotifierAndroid::~NetworkChangeNotifierAndroid() {
  ClearGlobalPointer();
  delegate_->UnregisterObserver(this);
}

NetworkChangeNotifier::ConnectionType
NetworkChangeNotifierAndroid::GetCurrentConnectionType() const {
  return delegate_->GetCurrentConnectionType();
}

NetworkChangeNotifier::ConnectionCost
NetworkChangeNotifierAndroid::GetCurrentConnectionCost() {
  return delegate_->GetCurrentConnectionCost();
}

NetworkChangeNotifier::ConnectionSubtype
NetworkChangeNotifierAndroid::GetCurrentConnectionSubtype() const {
  return delegate_->GetCurrentConnectionSubtype();
}

void NetworkChangeNotifierAndroid::GetCurrentMaxBandwidthAndConnectionType(
    double* max_bandwidth_mbps,
    ConnectionType* connection_type) const {
  delegate_->GetCurrentMaxBandwidthAndConnectionType(max_bandwidth_mbps,
                                                     connection_type);
}

void NetworkChangeNotifierAndroid::ForceNetworkHandlesSupportedForTesting() {
  force_network_handles_supported_for_testing_ = true;
}

bool NetworkChangeNotifierAndroid::AreNetworkHandlesCurrentlySupported() const {
  // Notifications for API using handles::NetworkHandles and querying using
  // handles::NetworkHandles only implemented for Android versions >= L.
  return force_network_handles_supported_for_testing_ ||
         (base::android::BuildInfo::GetInstance()->sdk_int() >=
              base::android::SDK_VERSION_LOLLIPOP &&
          !delegate_->RegisterNetworkCallbackFailed());
}

void NetworkChangeNotifierAndroid::GetCurrentConnectedNetworks(
    NetworkChangeNotifier::NetworkList* networks) const {
  delegate_->GetCurrentlyConnectedNetworks(networks);
}

NetworkChangeNotifier::ConnectionType
NetworkChangeNotifierAndroid::GetCurrentNetworkConnectionType(
    handles::NetworkHandle network) const {
  return delegate_->GetNetworkConnectionType(network);
}

handles::NetworkHandle NetworkChangeNotifierAndroid::GetCurrentDefaultNetwork()
    const {
  return delegate_->GetCurrentDefaultNetwork();
}

void NetworkChangeNotifierAndroid::OnConnectionTypeChanged() {
  BlockingThreadObjects::NotifyNetworkChangeNotifierObservers();
}

void NetworkChangeNotifierAndroid::OnConnectionCostChanged() {
  NetworkChangeNotifier::NotifyObserversOfConnectionCostChange();
}

void NetworkChangeNotifierAndroid::OnMaxBandwidthChanged(
    double max_bandwidth_mbps,
    ConnectionType type) {
  NetworkChangeNotifier::NotifyObserversOfMaxBandwidthChange(max_bandwidth_mbps,
                                                             type);
}

void NetworkChangeNotifierAndroid::OnNetworkConnected(
    handles::NetworkHandle network) {
  NetworkChangeNotifier::NotifyObserversOfSpecificNetworkChange(
      NetworkChangeType::kConnected, network);
}

void NetworkChangeNotifierAndroid::OnNetworkSoonToDisconnect(
    handles::NetworkHandle network) {
  NetworkChangeNotifier::NotifyObserversOfSpecificNetworkChange(
      NetworkChangeType::kSoonToDisconnect, network);
}

void NetworkChangeNotifierAndroid::OnNetworkDisconnected(
    handles::NetworkHandle network) {
  NetworkChangeNotifier::NotifyObserversOfSpecificNetworkChange(
      NetworkChangeType::kDisconnected, network);
}

void NetworkChangeNotifierAndroid::OnNetworkMadeDefault(
    handles::NetworkHandle network) {
  NetworkChangeNotifier::NotifyObserversOfSpecificNetworkChange(
      NetworkChangeType::kMadeDefault, network);
}

void NetworkChangeNotifierAndroid::OnDefaultNetworkActive() {
  NetworkChangeNotifier::NotifyObserversOfDefaultNetworkActive();
}

NetworkChangeNotifierAndroid::NetworkChangeNotifierAndroid(
    NetworkChangeNotifierDelegateAndroid* delegate)
    : NetworkChangeNotifier(NetworkChangeCalculatorParamsAndroid()),
      delegate_(delegate),
      blocking_thread_objects_(nullptr, base::OnTaskRunnerDeleter(nullptr)) {
  static_assert(NetId::INVALID == handles::kInvalidNetworkHandle,
                "handles::kInvalidNetworkHandle doesn't match NetId::INVALID");
  delegate_->RegisterObserver(this);
  // Since Android P, ConnectivityManager's signals include VPNs so we don't
  // need to use AddressTrackerLinux.
  if (base::android::BuildInfo::GetInstance()->sdk_int() <
      base::android::SDK_VERSION_P) {
    // |blocking_thread_objects_| will live on this runner.
    scoped_refptr<base::SequencedTaskRunner> blocking_thread_runner =
        base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()});
    blocking_thread_objects_ =
        std::unique_ptr<BlockingThreadObjects, base::OnTaskRunnerDeleter>(
            new BlockingThreadObjects(),
            // Ensure |blocking_thread_objects_| lives on
            // |blocking_thread_runner| to prevent races where
            // NetworkChangeNotifierAndroid outlives
            // TaskEnvironment. https://crbug.com/938126
            base::OnTaskRunnerDeleter(blocking_thread_runner));
    blocking_thread_runner->PostTask(
        FROM_HERE,
        base::BindOnce(&BlockingThreadObjects::Init,
                       // The Unretained pointer is safe here because it's
                       // posted before the deleter can post.
                       base::Unretained(blocking_thread_objects_.get())));
  }
}

// static
NetworkChangeNotifier::NetworkChangeCalculatorParams
NetworkChangeNotifierAndroid::NetworkChangeCalculatorParamsAndroid() {
  NetworkChangeCalculatorParams params;
  // IPAddressChanged is produced immediately prior to ConnectionTypeChanged
  // so delay IPAddressChanged so they get merged with the following
  // ConnectionTypeChanged signal.
  params.ip_address_offline_delay_ = base::Seconds(1);
  params.ip_address_online_delay_ = base::Seconds(1);
  params.connection_type_offline_delay_ = base::Seconds(0);
  params.connection_type_online_delay_ = base::Seconds(0);
  return params;
}

bool NetworkChangeNotifierAndroid::IsDefaultNetworkActiveInternal() {
  return delegate_->IsDefaultNetworkActive();
}

void NetworkChangeNotifierAndroid::DefaultNetworkActiveObserverAdded() {
  delegate_->DefaultNetworkActiveObserverAdded();
}

void NetworkChangeNotifierAndroid::DefaultNetworkActiveObserverRemoved() {
  delegate_->DefaultNetworkActiveObserverRemoved();
}

}  // namespace net
