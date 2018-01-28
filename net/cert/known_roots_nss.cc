// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/known_roots_nss.h"

#include <cert.h>
#include <pk11pub.h>

#include <memory>

namespace net {

// IsKnownRoot returns true if the given certificate is one that we believe
// is a standard (as opposed to user-installed) root.
bool IsKnownRoot(CERTCertificate* root) {
  if (!root || !root->slot)
    return false;

  // This magic name is taken from
  // http://bonsai.mozilla.org/cvsblame.cgi?file=mozilla/security/nss/lib/ckfw/builtins/constants.c&rev=1.13&mark=86,89#79
  return 0 == strcmp(PK11_GetSlotName(root->slot), "NSS Builtin Objects");
}

}  // namespace net
