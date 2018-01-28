// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TEST_CHANNEL_ID_TEST_UTIL_H_
#define NET_TEST_CHANNEL_ID_TEST_UTIL_H_

#include <string>

#include "base/compiler_specific.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace crypto {
class ECPrivateKey;
}  // namespace crypto

namespace net {

::testing::AssertionResult KeysEqual(crypto::ECPrivateKey* key1,
                                     crypto::ECPrivateKey* key2)
    WARN_UNUSED_RESULT;

}  // namespace net

#endif  //  NET_TEST_CHANNEL_ID_TEST_UTIL_H_
