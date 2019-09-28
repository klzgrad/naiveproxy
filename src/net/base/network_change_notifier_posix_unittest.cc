// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_change_notifier_posix.h"

#include <utility>

#include "base/test/scoped_task_environment.h"
#include "net/base/network_change_notifier.h"
#include "net/dns/system_dns_config_change_notifier.h"
#include "net/dns/test_dns_config_service.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace net {

class NetworkChangeNotifierPosixTest : public testing::Test {
 public:
  NetworkChangeNotifierPosixTest()
      : scoped_task_environment_(
            base::test::ScopedTaskEnvironment::TimeSource::MOCK_TIME),
        notifier_(new NetworkChangeNotifierPosix(
            NetworkChangeNotifier::CONNECTION_UNKNOWN,
            NetworkChangeNotifier::SUBTYPE_UNKNOWN)) {
    auto dns_config_service = std::make_unique<TestDnsConfigService>();
    dns_config_service_ = dns_config_service.get();
    notifier_->system_dns_config_notifier()->SetDnsConfigServiceForTesting(
        std::move(dns_config_service));
  }

  void FastForwardUntilIdle() {
    scoped_task_environment_.FastForwardUntilNoTasksRemain();
  }

  NetworkChangeNotifierPosix* notifier() { return notifier_.get(); }
  TestDnsConfigService* dns_config_service() { return dns_config_service_; }

 private:
  base::test::ScopedTaskEnvironment scoped_task_environment_;
  net::NetworkChangeNotifier::DisableForTest mock_notifier_disabler_;
  std::unique_ptr<NetworkChangeNotifierPosix> notifier_;
  TestDnsConfigService* dns_config_service_;
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

TEST_F(NetworkChangeNotifierPosixTest, OnDNSChanged) {
  DnsConfig expected_config;
  expected_config.nameservers = {IPEndPoint(IPAddress(1, 2, 3, 4), 233)};
  dns_config_service()->SetConfigForRefresh(expected_config);

  notifier()->OnDNSChanged();
  FastForwardUntilIdle();

  DnsConfig actual_config;
  NetworkChangeNotifier::GetDnsConfig(&actual_config);
  EXPECT_EQ(expected_config, actual_config);
}

}  // namespace net
