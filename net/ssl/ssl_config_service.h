// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_SSL_CONFIG_SERVICE_H_
#define NET_SSL_SSL_CONFIG_SERVICE_H_

#include <vector>

#include "base/memory/ref_counted.h"
#include "base/observer_list.h"
#include "net/base/net_export.h"
#include "net/cert/crl_set.h"
#include "net/ssl/ssl_config.h"

namespace net {

// The interface for retrieving the SSL configuration.  This interface
// does not cover setting the SSL configuration, as on some systems, the
// SSLConfigService objects may not have direct access to the configuration, or
// live longer than the configuration preferences.
class NET_EXPORT SSLConfigService
    : public base::RefCountedThreadSafe<SSLConfigService> {
 public:
  // Observer is notified when SSL config settings have changed.
  class NET_EXPORT Observer {
   public:
    // Notify observers if SSL settings have changed.  We don't check all of the
    // data in SSLConfig, just those that qualify as a user config change.
    // The following settings are considered user changes:
    //     rev_checking_enabled
    //     version_min
    //     version_max
    //     disabled_cipher_suites
    //     channel_id_enabled
    //     false_start_enabled
    //     require_forward_secrecy
    virtual void OnSSLConfigChanged() = 0;

   protected:
    virtual ~Observer() {}
  };

  SSLConfigService();

  // May not be thread-safe, should only be called on the IO thread.
  virtual void GetSSLConfig(SSLConfig* config) = 0;

  // Sets the current global CRL set to |crl_set|, if and only if the passed CRL
  // set has a higher sequence number (as reported by CRLSet::sequence()) than
  // the current set (or there is no current set). Can be called concurrently
  // with itself and with GetCRLSet.
  static void SetCRLSetIfNewer(scoped_refptr<CRLSet> crl_set);

  // Like SetCRLSetIfNewer() but assigns it unconditionally. Should only be used
  // by test code.
  static void SetCRLSetForTesting(scoped_refptr<CRLSet> crl_set);

  // Gets the current global CRL set. In the case that none exists, returns
  // nullptr.
  static scoped_refptr<CRLSet> GetCRLSet();

  // Add an observer of this service.
  void AddObserver(Observer* observer);

  // Remove an observer of this service.
  void RemoveObserver(Observer* observer);

  // Calls the OnSSLConfigChanged method of registered observers. Should only be
  // called on the IO thread.
  void NotifySSLConfigChange();

 protected:
  friend class base::RefCountedThreadSafe<SSLConfigService>;

  virtual ~SSLConfigService();

  // Process before/after config update.
  void ProcessConfigUpdate(const SSLConfig& orig_config,
                           const SSLConfig& new_config);

  static void SetCRLSet(scoped_refptr<CRLSet> crl_set, bool if_newer);

 private:
  base::ObserverList<Observer> observer_list_;
};

}  // namespace net

#endif  // NET_SSL_SSL_CONFIG_SERVICE_H_
