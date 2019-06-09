// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_SSL_CONFIG_SERVICE_H_
#define NET_SSL_SSL_CONFIG_SERVICE_H_

#include <vector>

#include "base/memory/ref_counted.h"
#include "base/observer_list.h"
#include "net/base/net_export.h"
#include "net/ssl/ssl_config.h"

namespace net {

// The interface for retrieving the SSL configuration.  This interface
// does not cover setting the SSL configuration, as on some systems, the
// SSLConfigService objects may not have direct access to the configuration, or
// live longer than the configuration preferences.
class NET_EXPORT SSLConfigService {
 public:
  // Observer is notified when SSL config settings have changed.
  class NET_EXPORT Observer {
   public:
    // Notify observers if SSL settings have changed.  We don't check all of the
    // data in SSLConfig, just those that qualify as a user config change.
    // The following settings are considered user changes:
    //     version_min
    //     version_max
    //     disabled_cipher_suites
    //     false_start_enabled
    //     require_ecdhe
    virtual void OnSSLConfigChanged() = 0;

   protected:
    virtual ~Observer() {}
  };

  SSLConfigService();
  virtual ~SSLConfigService();

  // May not be thread-safe, should only be called on the IO thread.
  virtual void GetSSLConfig(SSLConfig* config) = 0;

  // Returns true if connections to |hostname| can reuse, or are permitted to
  // reuse, connections on which a client cert has been negotiated. Note that
  // this must return true for both hostnames being pooled - that is to say this
  // function must return true for both the hostname of the existing connection
  // and the potential hostname to pool before allowing the connection to be
  // reused.
  //
  // NOTE: Pooling connections with ambient authority can create security issues
  // with that ambient authority and privacy issues in that embedders (and
  // users) may not have been consulted to send a client cert to |hostname|.
  // Implementations of this method should only return true if they have
  // received affirmative consent (e.g. through preferences or Enterprise
  // policy).
  //
  // NOTE: For Web Platform clients, this violates the Fetch Standard's policies
  // around connection pools: https://fetch.spec.whatwg.org/#connections.
  // Implementations that return true should take steps to limit the Web
  // Platform visibility of this, such as only allowing it to be used for
  // Enterprise or internal configurations.
  //
  // DEPRECATED: For the reasons above, this method is temporary and will be
  // removed in a future release. Please leave a comment on
  // https://crbug.com/855690 if you believe this is needed.
  virtual bool CanShareConnectionWithClientCerts(
      const std::string& hostname) const = 0;

  // Add an observer of this service.
  void AddObserver(Observer* observer);

  // Remove an observer of this service.
  void RemoveObserver(Observer* observer);

  // Calls the OnSSLConfigChanged method of registered observers. Should only be
  // called on the IO thread.
  void NotifySSLConfigChange();

  // Checks if the config-service managed fields in two SSLConfigs are the same.
  static bool SSLConfigsAreEqualForTesting(const net::SSLConfig& config1,
                                           const net::SSLConfig& config2);

 protected:
  // Process before/after config update. If |force_notification| is true,
  // NotifySSLConfigChange will be called regardless of whether |orig_config|
  // and |new_config| are equal.
  void ProcessConfigUpdate(const SSLConfig& orig_config,
                           const SSLConfig& new_config,
                           bool force_notification);

 private:
  base::ObserverList<Observer>::Unchecked observer_list_;
};

}  // namespace net

#endif  // NET_SSL_SSL_CONFIG_SERVICE_H_
