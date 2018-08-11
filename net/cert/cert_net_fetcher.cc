// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/cert_net_fetcher.h"

#include "base/lazy_instance.h"
#include "base/logging.h"

namespace net {

namespace {

base::LazyInstance<scoped_refptr<CertNetFetcher>>::Leaky g_cert_net_fetcher =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

CertNetFetcher* GetGlobalCertNetFetcher() {
  return g_cert_net_fetcher.Get().get();
}

void SetGlobalCertNetFetcher(scoped_refptr<CertNetFetcher> cert_net_fetcher) {
  DCHECK(!g_cert_net_fetcher.Get());
  g_cert_net_fetcher.Get() = std::move(cert_net_fetcher);
}

void SetGlobalCertNetFetcherForTesting(
    scoped_refptr<CertNetFetcher> cert_net_fetcher) {
  if (g_cert_net_fetcher.Get())
    g_cert_net_fetcher.Get()->Shutdown();
  g_cert_net_fetcher.Get() = std::move(cert_net_fetcher);
}

void ShutdownGlobalCertNetFetcher() {
  g_cert_net_fetcher.Get()->Shutdown();
  g_cert_net_fetcher.Get() = nullptr;
}

}  // namespace net
