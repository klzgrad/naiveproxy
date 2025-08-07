// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_CLIENT_CERT_STORE_NSS_H_
#define NET_SSL_CLIENT_CERT_STORE_NSS_H_

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "net/base/net_export.h"
#include "net/ssl/client_cert_matcher.h"
#include "net/ssl/client_cert_store.h"

typedef struct CERTCertListStr CERTCertList;
typedef struct CERTCertificateStr CERTCertificate;

namespace crypto {
class CryptoModuleBlockingPasswordDelegate;
}

namespace net {
class HostPortPair;
class SSLCertRequestInfo;

class NET_EXPORT ClientCertStoreNSS : public ClientCertStore {
 public:
  using PasswordDelegateFactory =
      base::RepeatingCallback<crypto::CryptoModuleBlockingPasswordDelegate*(
          const HostPortPair& /* server */)>;
  using CertFilter = base::RepeatingCallback<bool(CERTCertificate*)>;

  class IssuerSourceNSS : public ClientCertIssuerSource {
    std::vector<bssl::UniquePtr<CRYPTO_BUFFER>> GetCertsByName(
        base::span<const uint8_t> name) override;
  };

  explicit ClientCertStoreNSS(
      const PasswordDelegateFactory& password_delegate_factory);

  ClientCertStoreNSS(const ClientCertStoreNSS&) = delete;
  ClientCertStoreNSS& operator=(const ClientCertStoreNSS&) = delete;

  ~ClientCertStoreNSS() override;

  // ClientCertStore:
  void GetClientCerts(scoped_refptr<const SSLCertRequestInfo> cert_request_info,
                      ClientCertListCallback callback) override;

  // Examines the certificates in |identities| to find all certificates that
  // match the client certificate request in |request|, removing any that don't.
  // The remaining certs will be updated to include intermediates.
  // Must be called from a worker thread.
  static void FilterCertsOnWorkerThread(ClientCertIdentityList* identities,
                                        const SSLCertRequestInfo& request);

  // Retrieves all client certificates that are stored by NSS and adds them to
  // |identities|. |password_delegate| is used to unlock slots if required. If
  // |cert_filter| is not null, only certificates that it returns true on will
  // be added.
  // Must be called from a worker thread.
  static void GetPlatformCertsOnWorkerThread(
      scoped_refptr<crypto::CryptoModuleBlockingPasswordDelegate>
          password_delegate,
      const CertFilter& cert_filter,
      ClientCertIdentityList* identities);

 private:
  static ClientCertIdentityList GetAndFilterCertsOnWorkerThread(
      scoped_refptr<crypto::CryptoModuleBlockingPasswordDelegate>
          password_delegate,
      scoped_refptr<const SSLCertRequestInfo> request);

  void OnClientCertsResponse(ClientCertListCallback callback,
                             ClientCertIdentityList identities);

  // The factory for creating the delegate for requesting a password to a
  // PKCS#11 token. May be null.
  PasswordDelegateFactory password_delegate_factory_;

  base::WeakPtrFactory<ClientCertStoreNSS> weak_factory_{this};
};

}  // namespace net

#endif  // NET_SSL_CLIENT_CERT_STORE_NSS_H_
