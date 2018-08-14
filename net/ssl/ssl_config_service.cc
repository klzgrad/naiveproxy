// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/ssl_config_service.h"

#include <tuple>

#include "base/lazy_instance.h"
#include "base/synchronization/lock.h"
#include "net/ssl/ssl_config_service_defaults.h"

namespace net {

namespace {

// Checks if the config-service managed fields in two SSLConfigs are the same.
bool SSLConfigsAreEqual(const net::SSLConfig& config1,
                        const net::SSLConfig& config2) {
  return std::tie(config1.rev_checking_enabled,
                  config1.rev_checking_required_local_anchors,
                  config1.sha1_local_anchors_enabled,
                  config1.symantec_enforcement_disabled, config1.version_min,
                  config1.version_max, config1.tls13_variant,
                  config1.disabled_cipher_suites, config1.channel_id_enabled,
                  config1.false_start_enabled, config1.require_ecdhe) ==
         std::tie(config2.rev_checking_enabled,
                  config2.rev_checking_required_local_anchors,
                  config2.sha1_local_anchors_enabled,
                  config2.symantec_enforcement_disabled, config2.version_min,
                  config2.version_max, config2.tls13_variant,
                  config2.disabled_cipher_suites, config2.channel_id_enabled,
                  config2.false_start_enabled, config2.require_ecdhe);
}

}  // namespace

SSLConfigService::SSLConfigService()
    : observer_list_(base::ObserverListPolicy::EXISTING_ONLY) {}

SSLConfigService::~SSLConfigService() = default;

// GlobalSSLObject holds a reference to a global SSL object, such as the
// CRLSet. It simply wraps a lock  around a scoped_refptr so that getting a
// reference doesn't race with updating the global object.
template <class T>
class GlobalSSLObject {
 public:
  scoped_refptr<T> Get() const {
    base::AutoLock locked(lock_);
    return ssl_object_;
  }

  bool CompareAndSet(const scoped_refptr<T>& new_ssl_object,
                     const scoped_refptr<T>& old_ssl_object) {
    base::AutoLock locked(lock_);
    if (ssl_object_ != old_ssl_object)
      return false;
    ssl_object_ = new_ssl_object;
    return true;
  }

 private:
  scoped_refptr<T> ssl_object_;
  mutable base::Lock lock_;
};

typedef GlobalSSLObject<CRLSet> GlobalCRLSet;

base::LazyInstance<GlobalCRLSet>::Leaky g_crl_set = LAZY_INSTANCE_INITIALIZER;

// static
void SSLConfigService::SetCRLSetIfNewer(scoped_refptr<CRLSet> crl_set) {
  SetCRLSet(std::move(crl_set), /*if_newer=*/true);
}

// static
void SSLConfigService::SetCRLSetForTesting(scoped_refptr<CRLSet> crl_set) {
  SetCRLSet(std::move(crl_set), /*if_newer=*/false);
}

// static
scoped_refptr<CRLSet> SSLConfigService::GetCRLSet() {
  return g_crl_set.Get().Get();
}

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

// static
void SSLConfigService::SetCRLSet(scoped_refptr<CRLSet> crl_set, bool if_newer) {
  // Note: this can be called concurently with GetCRLSet().
  while (true) {
    scoped_refptr<CRLSet> old_crl_set(GetCRLSet());
    if (if_newer && old_crl_set && crl_set &&
        old_crl_set->sequence() >= crl_set->sequence()) {
      LOG(WARNING) << "Refusing to downgrade CRL set from #"
                   << old_crl_set->sequence() << " to #" << crl_set->sequence();
      break;
    }
    if (g_crl_set.Get().CompareAndSet(crl_set, old_crl_set))
      break;
  }
}

}  // namespace net
