// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "net/base/reconnect_notifier.h"

#include <utility>

#include "base/containers/contains.h"
#include "base/functional/bind.h"

namespace net {

ConnectionChangeNotifier::Observer::Observer() = default;
ConnectionChangeNotifier::Observer::~Observer() {
  if (notifier_) {
    notifier_->RemoveObserver(this);
  }
}

void ConnectionChangeNotifier::Observer::OnAttach(
    base::WeakPtr<ConnectionChangeNotifier> notifier) {
  CHECK(!notifier_);
  notifier_ = notifier;
}

void ConnectionChangeNotifier::OnSessionClosed() {
  observer_list_.Notify(&ConnectionChangeNotifier::Observer::OnSessionClosed);
}

void ConnectionChangeNotifier::OnConnectionFailed() {
  observer_list_.Notify(
      &ConnectionChangeNotifier::Observer::OnConnectionFailed);
}

void ConnectionChangeNotifier::OnNetworkEvent(NetworkChangeEvent event) {
  observer_list_.Notify(&ConnectionChangeNotifier::Observer::OnNetworkEvent,
                        event);
}

void ConnectionChangeNotifier::AddObserver(
    ConnectionChangeNotifier::Observer* observer) {
  CHECK(observer);
  observer_list_.AddObserver(observer);

  // Set the WeakPtr of `this` so that the observer can remove itself when
  // it is being destructed.
  observer->OnAttach(weak_factory_.GetWeakPtr());
}

void ConnectionChangeNotifier::RemoveObserver(const Observer* observer) {
  CHECK(observer);
  observer_list_.RemoveObserver(observer);
}

ConnectionChangeNotifier::ConnectionChangeNotifier() = default;
ConnectionChangeNotifier::~ConnectionChangeNotifier() = default;

ConnectionManagementConfig::ConnectionManagementConfig() = default;
ConnectionManagementConfig::~ConnectionManagementConfig() = default;

ConnectionManagementConfig::ConnectionManagementConfig(
    const ConnectionManagementConfig& other) = default;

ConnectionManagementConfig::ConnectionManagementConfig(
    ConnectionManagementConfig&& other) = default;
}  // namespace net
