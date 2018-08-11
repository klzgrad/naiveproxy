// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TEST_SPAWNED_TEST_SERVER_SPAWNED_TEST_SERVER_H_
#define NET_TEST_SPAWNED_TEST_SERVER_SPAWNED_TEST_SERVER_H_

#include "build/build_config.h"

#if defined(OS_ANDROID) || defined(OS_FUCHSIA)
#include "net/test/spawned_test_server/remote_test_server.h"
#else
#include "net/test/spawned_test_server/local_test_server.h"
#endif

namespace net {

#if defined(OS_ANDROID) || defined(OS_FUCHSIA)
typedef RemoteTestServer SpawnedTestServer;
#else
typedef LocalTestServer SpawnedTestServer;
#endif

}  // namespace net

#endif  // NET_TEST_SPAWNED_TEST_SERVER_SPAWNED_TEST_SERVER_H_
