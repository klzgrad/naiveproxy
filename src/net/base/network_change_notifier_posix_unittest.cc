// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_change_notifier_posix.h"

#include "base/test/scoped_task_environment.h"
#include "net/base/network_change_notifier.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace net {

class NetworkChangeNotifierPosixTest : public testing::Test {
 public:
  NetworkChangeNotifierPosixTest()
      : scoped_task_environment_(
            base::test::ScopedTaskEnvironment::MainThreadType::MOCK_TIME),
        notifier_(new NetworkChangeNotifierPosix(
            NetworkChangeNotifier::CONNECTION_UNKNOWN,
            NetworkChangeNotifier::SUBTYPE_UNKNOWN)) {}

  void FastForwardUntilIdle() {
    scoped_task_environment_.FastForwardUntilNoTasksRemain();
  }

  NetworkChangeNotifierPosix* notifier() { return notifier_.get(); }

 private:
  base::test::ScopedTaskEnvironment scoped_task_environment_;
  net::NetworkChangeNotifier::DisableForTest mock_notifier_disabler_;
  std::unique_ptr<NetworkChangeNotifierPosix> notifier_;
};

class MockIPAddressObserver : public NetworkChangeNotifier::IPAddressObserver {
 public:
  MOCK_METHOD0(OnIPAddressChanged, void());
};

TEST_F(NetworkChangeNotifierPosixTest, OnIPAddressChanged) {
  testing::StrictMock<MockIPAddressObserver> observer;
  NetworkChangeNotifier::AddIPAddressObserver(&observer);

  EXPECT_CALL(observer, OnIPAddressChanged());
  notifier()->OnIPAddressChanged();
  FastForwardUntilIdle();

  NetworkChangeNotifier::RemoveIPAddressObserver(&observer);
}

class MockNetworkChangeObserver
    : public NetworkChangeNotifier::NetworkChangeObserver {
 public:
  MOCK_METHOD1(OnNetworkChanged, void(NetworkChangeNotifier::ConnectionType));
};

TEST_F(NetworkChangeNotifierPosixTest, OnNetworkChanged) {
  testing::StrictMock<MockNetworkChangeObserver> observer;
  NetworkChangeNotifier::AddNetworkChangeObserver(&observer);

  EXPECT_CALL(observer,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_NONE));
  EXPECT_CALL(observer, OnNetworkChanged(NetworkChangeNotifier::CONNECTION_3G));
  notifier()->OnConnectionChanged(NetworkChangeNotifier::CONNECTION_3G);
  FastForwardUntilIdle();

  NetworkChangeNotifier::RemoveNetworkChangeObserver(&observer);
}

class MockMaxBandwidthObserver
    : public NetworkChangeNotifier::MaxBandwidthObserver {
 public:
  MOCK_METHOD2(OnMaxBandwidthChanged,
               void(double, NetworkChangeNotifier::ConnectionType));
};

TEST_F(NetworkChangeNotifierPosixTest, OnMaxBandwidthChanged) {
  testing::StrictMock<MockMaxBandwidthObserver> observer;
  NetworkChangeNotifier::AddMaxBandwidthObserver(&observer);

  EXPECT_CALL(observer,
              OnMaxBandwidthChanged(3.6, NetworkChangeNotifier::CONNECTION_4G));
  notifier()->OnConnectionSubtypeChanged(NetworkChangeNotifier::CONNECTION_4G,
                                         NetworkChangeNotifier::SUBTYPE_HSPA);
  FastForwardUntilIdle();

  NetworkChangeNotifier::RemoveMaxBandwidthObserver(&observer);
}

}  // namespace net
