// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/cert_database.h"

#include "base/observer_list_threadsafe.h"
#include "crypto/nss_util.h"

namespace net {

CertDatabase::CertDatabase()
    : observer_list_(new base::ObserverListThreadSafe<Observer>) {
  crypto::EnsureNSSInit();
}

CertDatabase::~CertDatabase() = default;

}  // namespace net
