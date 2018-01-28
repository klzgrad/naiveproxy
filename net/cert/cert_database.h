// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_CERT_DATABASE_H_
#define NET_CERT_CERT_DATABASE_H_

#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "net/base/net_export.h"
#include "net/cert/x509_certificate.h"

namespace base {
template <typename T> struct DefaultSingletonTraits;

template <class ObserverType>
class ObserverListThreadSafe;
}

namespace net {

// This class provides cross-platform functions to verify and add user
// certificates, and to observe changes to the underlying certificate stores.

// TODO(gauravsh): This class could be augmented with methods
// for all operations that manipulate the underlying system
// certificate store.

class NET_EXPORT CertDatabase {
 public:
  // A CertDatabase::Observer will be notified on certificate database changes.
  // The change could be either a user certificate is added/removed or trust on
  // a certificate is changed. Observers can be registered via
  // CertDatabase::AddObserver, and can un-register with
  // CertDatabase::RemoveObserver.
  class NET_EXPORT Observer {
   public:
    virtual ~Observer() {}

    // Called whenever the Cert Database is known to have changed.
    // Typically, this will be in response to a CA certificate being added,
    // removed, or its trust changed, but may also signal on client
    // certificate events when they can be reliably detected.
    virtual void OnCertDBChanged() {}

   protected:
    Observer() {}

   private:
    DISALLOW_COPY_AND_ASSIGN(Observer);
  };

  // Returns the CertDatabase singleton.
  static CertDatabase* GetInstance();

  // Registers |observer| to receive notifications of certificate changes.  The
  // thread on which this is called is the thread on which |observer| will be
  // called back with notifications.
  void AddObserver(Observer* observer);

  // Unregisters |observer| from receiving notifications.  This must be called
  // on the same thread on which AddObserver() was called.
  void RemoveObserver(Observer* observer);

#if defined(OS_MACOSX) && !defined(OS_IOS)
  // Configures the current message loop to observe and forward events from
  // Keychain services. The MessageLoop must have an associated CFRunLoop,
  // which means that this must be called from a MessageLoop of TYPE_UI.
  void SetMessageLoopForKeychainEvents();
#endif

#if defined(OS_ANDROID)
  // On Android, the system key store may be replaced with a device-specific
  // KeyStore used for storing client certificates. When the Java side replaces
  // the KeyStore used for client certificates, notifies the observers as if a
  // new client certificate was added.
  void OnAndroidKeyStoreChanged();

  // On Android, the system database is used. When the system notifies the
  // application that the certificates changed, the observers must be notified.
  void OnAndroidKeyChainChanged();
#endif

  // Synthetically injects notifications to all observers. In general, this
  // should only be called by the creator of the CertDatabase. Used to inject
  // notifcations from other DB interfaces.
  void NotifyObserversCertDBChanged();

 private:
  friend struct base::DefaultSingletonTraits<CertDatabase>;

  CertDatabase();
  ~CertDatabase();

  const scoped_refptr<base::ObserverListThreadSafe<Observer>> observer_list_;

#if defined(OS_MACOSX) && !defined(OS_IOS)
  class Notifier;
  friend class Notifier;
  std::unique_ptr<Notifier> notifier_;
#endif

  DISALLOW_COPY_AND_ASSIGN(CertDatabase);
};

}  // namespace net

#endif  // NET_CERT_CERT_DATABASE_H_
