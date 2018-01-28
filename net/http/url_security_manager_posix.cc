// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/url_security_manager.h"

#include "net/http/http_auth_filter.h"

namespace net {

// static
URLSecurityManager* URLSecurityManager::Create() {
  return new URLSecurityManagerWhitelist;
}

}  //  namespace net
