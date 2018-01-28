// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_CLIENT_CERT_STORE_WIN_H_
#define NET_SSL_CLIENT_CERT_STORE_WIN_H_

#include "base/callback.h"
#include "base/macros.h"
#include "crypto/scoped_capi_types.h"
#include "net/base/net_export.h"
#include "net/ssl/client_cert_store.h"
#include "net/ssl/ssl_cert_request_info.h"

namespace net {

class NET_EXPORT ClientCertStoreWin : public ClientCertStore {
 public:
  // Uses the "MY" current user system certificate store.
  ClientCertStoreWin();

  // Takes ownership of |cert_store| and closes it at destruction time.
  explicit ClientCertStoreWin(HCERTSTORE cert_store);

  ~ClientCertStoreWin() override;

  // If a cert store has been provided at construction time GetClientCerts
  // will use that. Otherwise it will use the current user's "MY" cert store
  // instead.
  void GetClientCerts(const SSLCertRequestInfo& cert_request_info,
                      const ClientCertListCallback& callback) override;

 private:
  using ScopedHCERTSTORE = crypto::ScopedCAPIHandle<
      HCERTSTORE,
      crypto::CAPIDestroyerWithFlags<HCERTSTORE,
                                     CertCloseStore,
                                     CERT_CLOSE_STORE_CHECK_FLAG>>;

  friend class ClientCertStoreWinTestDelegate;

  // Opens the "MY" cert store and uses it to lookup the client certs.
  static ClientCertIdentityList GetClientCertsWithMyCertStore(
      const SSLCertRequestInfo& request);

  // A hook for testing. Filters |input_certs| using the logic being used to
  // filter the system store when GetClientCerts() is called.
  // Implemented by creating a temporary in-memory store and filtering it
  // using the common logic.
  bool SelectClientCertsForTesting(const CertificateList& input_certs,
                                   const SSLCertRequestInfo& cert_request_info,
                                   ClientCertIdentityList* selected_identities);

  ScopedHCERTSTORE cert_store_;

  DISALLOW_COPY_AND_ASSIGN(ClientCertStoreWin);
};

}  // namespace net

#endif  // NET_SSL_CLIENT_CERT_STORE_WIN_H_
