// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/ssl_config_service.h"

#include <tuple>

#include "net/ssl/ssl_config_service_defaults.h"

namespace net {

namespace {

// Checks if the config-service managed fields in two SSLConfigs are the same.
bool SSLConfigsAreEqual(const net::SSLConfig& config1,
                        const net::SSLConfig& config2) {
  return std::tie(config1.version_min, config1.version_max,
                  config1.tls13_variant, config1.disabled_cipher_suites,
                  config1.channel_id_enabled, config1.false_start_enabled,
                  config1.require_ecdhe) ==
         std::tie(config2.version_min, config2.version_max,
                  config2.tls13_variant, config2.disabled_cipher_suites,
                  config2.channel_id_enabled, config2.false_start_enabled,
                  config2.require_ecdhe);
}

}  // namespace

SSLConfigService::SSLConfigService()
    : observer_list_(base::ObserverListPolicy::EXISTING_ONLY) {}

SSLConfigService::~SSLConfigService() = default;

void SSLConfigService::AddObserver(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void SSLConfigService::RemoveObserver(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

void SSLConfigService::NotifySSLConfigChange() {
  for (auto& observer : observer_list_)
    observer.OnSSLConfigChanged();
}

bool SSLConfigService::SSLConfigsAreEqualForTesting(
    const net::SSLConfig& config1,
    const net::SSLConfig& config2) {
  return SSLConfigsAreEqual(config1, config2);
}

void SSLConfigService::ProcessConfigUpdate(const SSLConfig& old_config,
                                           const SSLConfig& new_config,
                                           bool force_notification) {
  // Do nothing if the configuration hasn't changed.
  if (!SSLConfigsAreEqual(old_config, new_config) || force_notification)
    NotifySSLConfigChange();
}

}  // namespace net
