// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/test/channel_id_test_util.h"

#include <string>

#include "crypto/ec_private_key.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

::testing::AssertionResult KeysEqual(crypto::ECPrivateKey* key1,
                                     crypto::ECPrivateKey* key2) {
  std::string public_key1, public_key2;
  EXPECT_TRUE(key1 && key1->ExportRawPublicKey(&public_key1));
  EXPECT_TRUE(key2 && key2->ExportRawPublicKey(&public_key2));
  if (public_key1 == public_key2)
    return ::testing::AssertionSuccess();
  else
    return ::testing::AssertionFailure();
}

}  // namespace net
