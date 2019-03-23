// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CERT_NET_CERT_NET_FETCHER_IMPL_H_
#define NET_CERT_NET_CERT_NET_FETCHER_IMPL_H_

#include "base/memory/ref_counted.h"
#include "net/base/net_export.h"

namespace net {

class CertNetFetcher;
class URLRequestContext;

// Creates a CertNetFetcher that issues requests through the provided
// URLRequestContext. The URLRequestContext must stay valid until the returned
// CertNetFetcher's Shutdown method is called. The CertNetFetcher is to be
// created and shutdown on the network thread. Its Fetch methods are to be used
// on a *different* thread, since it gives a blocking interface to URL fetching.
NET_EXPORT scoped_refptr<CertNetFetcher> CreateCertNetFetcher(
    URLRequestContext* context);

}  // namespace net

#endif  // NET_CERT_NET_CERT_NET_FETCHER_IMPL_H_
