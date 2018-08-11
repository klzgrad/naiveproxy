// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/logging_network_change_observer.h"

#include <string>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "net/log/net_log.h"
#include "net/log/net_log_event_type.h"

#if defined(OS_ANDROID)
#include "base/android/build_info.h"
#endif

namespace net {

namespace {

// Returns a human readable integer from a NetworkHandle.
int HumanReadableNetworkHandle(NetworkChangeNotifier::NetworkHandle network) {
#if defined(OS_ANDROID)
  // On Marshmallow, demunge the NetID to undo munging done in java
  // Network.getNetworkHandle() by shifting away 0xfacade from
  // http://androidxref.com/6.0.1_r10/xref/frameworks/base/core/java/android/net/Network.java#385
  if (base::android::BuildInfo::GetInstance()->sdk_int() >=
      base::android::SDK_VERSION_MARSHMALLOW) {
    return network >> 32;
  }
#endif
  return network;
}

// Return a dictionary of values that provide information about a
// network-specific change. This also includes relevant current state
// like the default network, and the types of active networks.
std::unique_ptr<base::Value> NetworkSpecificNetLogCallback(
    NetworkChangeNotifier::NetworkHandle network,
    NetLogCaptureMode capture_mode) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetInteger("changed_network_handle",
                   HumanReadableNetworkHandle(network));
  dict->SetString(
      "changed_network_type",
      NetworkChangeNotifier::ConnectionTypeToString(
          NetworkChangeNotifier::GetNetworkConnectionType(network)));
  dict->SetInteger(
      "default_active_network_handle",
      HumanReadableNetworkHandle(NetworkChangeNotifier::GetDefaultNetwork()));
  NetworkChangeNotifier::NetworkList networks;
  NetworkChangeNotifier::GetConnectedNetworks(&networks);
  for (NetworkChangeNotifier::NetworkHandle active_network : networks) {
    dict->SetString(
        "current_active_networks." +
            base::IntToString(HumanReadableNetworkHandle(active_network)),
        NetworkChangeNotifier::ConnectionTypeToString(
            NetworkChangeNotifier::GetNetworkConnectionType(active_network)));
  }
  return std::move(dict);
}

}  // namespace

LoggingNetworkChangeObserver::LoggingNetworkChangeObserver(NetLog* net_log)
    : net_log_(net_log) {
  NetworkChangeNotifier::AddIPAddressObserver(this);
  NetworkChangeNotifier::AddConnectionTypeObserver(this);
  NetworkChangeNotifier::AddNetworkChangeObserver(this);
  if (NetworkChangeNotifier::AreNetworkHandlesSupported())
    NetworkChangeNotifier::AddNetworkObserver(this);
}

LoggingNetworkChangeObserver::~LoggingNetworkChangeObserver() {
  NetworkChangeNotifier::RemoveIPAddressObserver(this);
  NetworkChangeNotifier::RemoveConnectionTypeObserver(this);
  NetworkChangeNotifier::RemoveNetworkChangeObserver(this);
  if (NetworkChangeNotifier::AreNetworkHandlesSupported())
    NetworkChangeNotifier::RemoveNetworkObserver(this);
}

void LoggingNetworkChangeObserver::OnIPAddressChanged() {
  VLOG(1) << "Observed a change to the network IP addresses";

  net_log_->AddGlobalEntry(NetLogEventType::NETWORK_IP_ADDRESSES_CHANGED);
}

void LoggingNetworkChangeObserver::OnConnectionTypeChanged(
    NetworkChangeNotifier::ConnectionType type) {
  std::string type_as_string =
      NetworkChangeNotifier::ConnectionTypeToString(type);

  VLOG(1) << "Observed a change to network connectivity state "
          << type_as_string;

  net_log_->AddGlobalEntry(
      NetLogEventType::NETWORK_CONNECTIVITY_CHANGED,
      NetLog::StringCallback("new_connection_type", &type_as_string));
}

void LoggingNetworkChangeObserver::OnNetworkChanged(
    NetworkChangeNotifier::ConnectionType type) {
  std::string type_as_string =
      NetworkChangeNotifier::ConnectionTypeToString(type);

  VLOG(1) << "Observed a network change to state " << type_as_string;

  net_log_->AddGlobalEntry(
      NetLogEventType::NETWORK_CHANGED,
      NetLog::StringCallback("new_connection_type", &type_as_string));
}

void LoggingNetworkChangeObserver::OnNetworkConnected(
    NetworkChangeNotifier::NetworkHandle network) {
  VLOG(1) << "Observed network " << network << " connect";

  net_log_->AddGlobalEntry(NetLogEventType::SPECIFIC_NETWORK_CONNECTED,
                           base::Bind(&NetworkSpecificNetLogCallback, network));
}

void LoggingNetworkChangeObserver::OnNetworkDisconnected(
    NetworkChangeNotifier::NetworkHandle network) {
  VLOG(1) << "Observed network " << network << " disconnect";

  net_log_->AddGlobalEntry(NetLogEventType::SPECIFIC_NETWORK_DISCONNECTED,
                           base::Bind(&NetworkSpecificNetLogCallback, network));
}

void LoggingNetworkChangeObserver::OnNetworkSoonToDisconnect(
    NetworkChangeNotifier::NetworkHandle network) {
  VLOG(1) << "Observed network " << network << " soon to disconnect";

  net_log_->AddGlobalEntry(NetLogEventType::SPECIFIC_NETWORK_SOON_TO_DISCONNECT,
                           base::Bind(&NetworkSpecificNetLogCallback, network));
}

void LoggingNetworkChangeObserver::OnNetworkMadeDefault(
    NetworkChangeNotifier::NetworkHandle network) {
  VLOG(1) << "Observed network " << network << " made the default network";

  net_log_->AddGlobalEntry(NetLogEventType::SPECIFIC_NETWORK_MADE_DEFAULT,
                           base::Bind(&NetworkSpecificNetLogCallback, network));
}

}  // namespace net
