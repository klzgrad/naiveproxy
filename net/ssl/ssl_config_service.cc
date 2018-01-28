// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/ssl/ssl_config_service.h"

#include <tuple>

#include "base/lazy_instance.h"
#include "base/synchronization/lock.h"
#include "net/ssl/ssl_config_service_defaults.h"

namespace net {

SSLConfigService::SSLConfigService()
    : observer_list_(base::ObserverListPolicy::EXISTING_ONLY) {}

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

SSLConfigService::~SSLConfigService() = default;

void SSLConfigService::ProcessConfigUpdate(const SSLConfig& old_config,
                                           const SSLConfig& new_config) {
  bool config_changed =
      std::tie(old_config.rev_checking_enabled,
               old_config.rev_checking_required_local_anchors,
               old_config.sha1_local_anchors_enabled,
               old_config.common_name_fallback_local_anchors_enabled,
               old_config.version_min, old_config.version_max,
               old_config.tls13_variant, old_config.disabled_cipher_suites,
               old_config.channel_id_enabled, old_config.false_start_enabled,
               old_config.require_ecdhe) !=
      std::tie(new_config.rev_checking_enabled,
               new_config.rev_checking_required_local_anchors,
               new_config.sha1_local_anchors_enabled,
               new_config.common_name_fallback_local_anchors_enabled,
               new_config.version_min, new_config.version_max,
               new_config.tls13_variant, new_config.disabled_cipher_suites,
               new_config.channel_id_enabled, new_config.false_start_enabled,
               new_config.require_ecdhe);

  if (config_changed)
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
