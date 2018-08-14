// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cookies/cookie_change_dispatcher.h"

namespace net {

bool CookieChangeCauseIsDeletion(CookieChangeCause cause) {
  return cause != CookieChangeCause::INSERTED;
}

}  // namespace net
