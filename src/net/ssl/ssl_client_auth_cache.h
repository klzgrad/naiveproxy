// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_SSL_SSL_CLIENT_AUTH_CACHE_H_
#define NET_SSL_SSL_CLIENT_AUTH_CACHE_H_

#include <map>
#include <string>
#include <utility>

#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_export.h"
#include "net/cert/cert_database.h"
#include "net/ssl/ssl_private_key.h"

namespace net {

class X509Certificate;

// The SSLClientAuthCache class is a simple cache structure to store SSL
// client certificates. Provides lookup, insertion, and deletion of entries.
// The parameter for doing lookups, insertions, and deletions is the server's
// host and port.
//
// TODO(wtc): This class is based on FtpAuthCache.  We can extract the common
// code to a template class.
class NET_EXPORT_PRIVATE SSLClientAuthCache : public CertDatabase::Observer {
 public:
  SSLClientAuthCache();
  ~SSLClientAuthCache() override;

  // Checks for a client certificate preference for SSL server at |server|.
  // Returns true if a preference is found, and sets |*certificate| to the
  // desired client certificate. The desired certificate may be NULL, which
  // indicates a preference to not send any certificate to |server|.
  // If a certificate preference is not found, returns false.
  bool Lookup(const HostPortPair& server,
              scoped_refptr<X509Certificate>* certificate,
              scoped_refptr<SSLPrivateKey>* private_key);

  // Add a client certificate and private key for |server| to the cache. If
  // there is already a client certificate for |server|, it will be
  // overwritten. A NULL |client_cert| indicates a preference that no client
  // certificate should be sent to |server|.
  void Add(const HostPortPair& server,
           scoped_refptr<X509Certificate> client_cert,
           scoped_refptr<SSLPrivateKey> private_key);

  // Remove the client certificate for |server| from the cache, if one exists.
  void Remove(const HostPortPair& server);

  // CertDatabase::Observer methods:
  void OnCertDBChanged() override;

 private:
  typedef HostPortPair AuthCacheKey;
  typedef std::pair<scoped_refptr<X509Certificate>,
                    scoped_refptr<SSLPrivateKey>> AuthCacheValue;
  typedef std::map<AuthCacheKey, AuthCacheValue> AuthCacheMap;

  // internal representation of cache, an STL map.
  AuthCacheMap cache_;
};

}  // namespace net

#endif  // NET_SSL_SSL_CLIENT_AUTH_CACHE_H_
