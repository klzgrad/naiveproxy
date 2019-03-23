// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_change_notifier_fuchsia.h"

#include <fuchsia/hardware/ethernet/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl_test_base.h>
#include <memory>
#include <utility>
#include <vector>

#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "net/base/ip_address.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace {

const int kDefaultNic = 1;
const int kSecondaryNic = kDefaultNic + 1;

fuchsia::net::IpAddress CreateIPv6Address(std::vector<uint8_t> addr) {
  fuchsia::net::IpAddress output;
  for (size_t i = 0; i < addr.size(); ++i) {
    output.ipv6().addr[i] = addr[i];
  }
  return output;
}

fuchsia::net::Subnet CreateSubnet(const std::vector<uint8_t>& addr,
                                  uint8_t prefix) {
  fuchsia::net::Subnet output;
  output.addr = CreateIPv6Address(addr);
  output.prefix_len = prefix;
  return output;
}

fuchsia::net::IpAddress CreateIPv4Address(uint8_t a0,
                                          uint8_t a1,
                                          uint8_t a2,
                                          uint8_t a3) {
  fuchsia::net::IpAddress output;
  output.ipv4().addr[0] = a0;
  output.ipv4().addr[1] = a1;
  output.ipv4().addr[2] = a2;
  output.ipv4().addr[3] = a3;
  return output;
}

fuchsia::netstack::RouteTableEntry CreateRouteTableEntry(uint32_t nicid,
                                                         bool is_default) {
  fuchsia::netstack::RouteTableEntry output;
  output.nicid = nicid;

  if (is_default) {
    output.netmask = CreateIPv4Address(0, 0, 0, 0);
    output.destination = CreateIPv4Address(192, 168, 42, 0);
    output.gateway = CreateIPv4Address(192, 168, 42, 1);
  } else {
    output.netmask = CreateIPv4Address(255, 255, 255, 0);
    output.destination = CreateIPv4Address(192, 168, 43, 0);
    output.gateway = CreateIPv4Address(192, 168, 43, 1);
  }

  return output;
}

fuchsia::netstack::NetInterface CreateNetInterface(
    uint32_t id,
    uint32_t flags,
    uint32_t features,
    fuchsia::net::IpAddress address,
    fuchsia::net::IpAddress netmask,
    std::vector<fuchsia::net::Subnet> ipv6) {
  fuchsia::netstack::NetInterface output;
  output.name = "foo";
  output.id = id;
  output.flags = flags;
  output.features = features;
  output.addr = std::move(address);
  output.netmask = std::move(netmask);

  output.addr.Clone(&output.broadaddr);

  for (auto& x : ipv6) {
    output.ipv6addrs.push_back(std::move(x));
  }

  return output;
}

// Partial fake implementation of a Netstack.
// GMock is not used because the methods make heavy use of move-only datatypes,
// which aren't handled well by GMock.
class FakeNetstack : public fuchsia::netstack::testing::Netstack_TestBase {
 public:
  explicit FakeNetstack(
      fidl::InterfaceRequest<fuchsia::netstack::Netstack> netstack_request)
      : binding_(this) {
    CHECK_EQ(ZX_OK, binding_.Bind(std::move(netstack_request)));
  }
  ~FakeNetstack() override = default;

  // Adds |interface| to the interface query response list.
  void PushInterface(fuchsia::netstack::NetInterface&& interface) {
    interfaces_.push_back(std::move(interface));
  }

  void PushRouteTableEntry(fuchsia::netstack::RouteTableEntry&& interface) {
    route_table_.push_back(std::move(interface));
  }

  // Sends the accumulated |interfaces_| to the OnInterfacesChanged event.
  void NotifyInterfaces() {
    binding_.events().OnInterfacesChanged(std::move(interfaces_));
    interfaces_.clear();
  }

  fidl::Binding<fuchsia::netstack::Netstack>& binding() { return binding_; }

 private:
  void GetInterfaces(GetInterfacesCallback callback) override {
    callback(std::move(interfaces_));
  }

  void GetRouteTable(GetRouteTableCallback callback) override {
    std::vector<fuchsia::netstack::RouteTableEntry> table(2);
    table[0] = CreateRouteTableEntry(kDefaultNic, true);
    table[1] = CreateRouteTableEntry(kSecondaryNic, false);
    callback(std::move(table));
  }

  void NotImplemented_(const std::string& name) override {
    LOG(FATAL) << "Unimplemented function called: " << name;
  }

  std::vector<fuchsia::netstack::NetInterface> interfaces_;
  std::vector<fuchsia::netstack::RouteTableEntry> route_table_;

  fidl::Binding<fuchsia::netstack::Netstack> binding_;

  DISALLOW_COPY_AND_ASSIGN(FakeNetstack);
};

class MockNetworkChangeObserver
    : public NetworkChangeNotifier::NetworkChangeObserver {
 public:
  MOCK_METHOD1(OnNetworkChanged, void(NetworkChangeNotifier::ConnectionType));
};

}  // namespace

class NetworkChangeNotifierFuchsiaTest : public testing::Test {
 public:
  NetworkChangeNotifierFuchsiaTest() : netstack_(netstack_ptr_.NewRequest()) {}

  ~NetworkChangeNotifierFuchsiaTest() override {}

  // Creates a NetworkChangeNotifier, which will be seeded with the list of
  // interfaces which have already been added to |netstack_|.
  void CreateNotifier(uint32_t required_features = 0) {
    notifier_.reset(new NetworkChangeNotifierFuchsia(std::move(netstack_ptr_),
                                                     required_features));
    NetworkChangeNotifier::AddNetworkChangeObserver(&observer_);
  }

  void TearDown() override {
    if (notifier_) {
      NetworkChangeNotifier::RemoveNetworkChangeObserver(&observer_);
    }
  }

 protected:
  base::MessageLoopForIO message_loop_;
  testing::StrictMock<MockNetworkChangeObserver> observer_;
  fuchsia::netstack::NetstackPtr netstack_ptr_;
  FakeNetstack netstack_;

  // Allows us to allocate our own NetworkChangeNotifier for unit testing.
  NetworkChangeNotifier::DisableForTest disable_for_test_;
  std::unique_ptr<NetworkChangeNotifierFuchsia> notifier_;

  testing::InSequence seq_;

 private:
  DISALLOW_COPY_AND_ASSIGN(NetworkChangeNotifierFuchsiaTest);
};

TEST_F(NetworkChangeNotifierFuchsiaTest, NoChange) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.PushRouteTableEntry(CreateRouteTableEntry(kDefaultNic, true));

  CreateNotifier();
  base::RunLoop().RunUntilIdle();

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.PushRouteTableEntry(CreateRouteTableEntry(kDefaultNic, true));
  netstack_.NotifyInterfaces();
  base::RunLoop().RunUntilIdle();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, NoChangeV6) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv6Address({0xfe, 0x80, 0x01}),
                         CreateIPv6Address({0xfe, 0x80}), {}));
  CreateNotifier();
  base::RunLoop().RunUntilIdle();

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv6Address({0xfe, 0x80, 0x01}),
                         CreateIPv6Address({0xfe, 0x80}), {}));
  netstack_.NotifyInterfaces();
  base::RunLoop().RunUntilIdle();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, MultiInterfaceNoChange) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.PushInterface(
      CreateNetInterface(kSecondaryNic, fuchsia::netstack::NetInterfaceFlagUp,
                         0, CreateIPv4Address(169, 254, 0, 2),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  CreateNotifier();
  base::RunLoop().RunUntilIdle();

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.PushInterface(
      CreateNetInterface(kSecondaryNic, fuchsia::netstack::NetInterfaceFlagUp,
                         0, CreateIPv4Address(169, 254, 0, 3),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.NotifyInterfaces();
  base::RunLoop().RunUntilIdle();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, MultiV6IPNoChange) {
  std::vector<fuchsia::net::Subnet> addresses;
  addresses.push_back(CreateSubnet({0xfe, 0x80, 0x01}, 2));
  netstack_.PushInterface(CreateNetInterface(
      kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
      CreateIPv4Address(169, 254, 0, 1), CreateIPv4Address(255, 255, 255, 0),
      std::move(addresses)));
  CreateNotifier();
  base::RunLoop().RunUntilIdle();

  addresses.push_back(CreateSubnet({0xfe, 0x80, 0x01}, 2));
  netstack_.PushInterface(CreateNetInterface(
      kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
      CreateIPv4Address(169, 254, 0, 1), CreateIPv4Address(255, 255, 255, 0),
      std::move(addresses)));
  netstack_.NotifyInterfaces();
  base::RunLoop().RunUntilIdle();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, IpChange) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  CreateNotifier();
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(observer_,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_NONE));
  EXPECT_CALL(observer_,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_UNKNOWN));
  netstack_.PushInterface(CreateNetInterface(
      kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
      CreateIPv4Address(10, 0, 0, 1), CreateIPv4Address(255, 255, 0, 0), {}));
  netstack_.NotifyInterfaces();
  base::RunLoop().RunUntilIdle();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, IpChangeV6) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv6Address({0xfe, 0x80, 0x01}),
                         CreateIPv6Address({0xfe, 0x80}), {}));
  CreateNotifier();
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(observer_,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_NONE));
  EXPECT_CALL(observer_,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_UNKNOWN));
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv6Address({0xfe, 0x80, 0x02}),
                         CreateIPv6Address({0xfe, 0x80}), {}));
  netstack_.NotifyInterfaces();
  base::RunLoop().RunUntilIdle();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, MultiV6IPChanged) {
  std::vector<fuchsia::net::Subnet> addresses;
  addresses.push_back(CreateSubnet({0xfe, 0x80, 0x01}, 2));
  netstack_.PushInterface(CreateNetInterface(
      kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
      CreateIPv4Address(169, 254, 0, 1), CreateIPv4Address(255, 255, 255, 0),
      std::move(addresses)));
  CreateNotifier();
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(observer_,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_NONE));
  EXPECT_CALL(observer_,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_UNKNOWN));
  addresses.push_back(CreateSubnet({0xfe, 0x80, 0x02}, 2));
  netstack_.PushInterface(CreateNetInterface(
      kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
      CreateIPv4Address(10, 0, 0, 1), CreateIPv4Address(255, 255, 0, 0),
      std::move(addresses)));
  netstack_.NotifyInterfaces();
  base::RunLoop().RunUntilIdle();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, Ipv6AdditionalIpChange) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  CreateNotifier();
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(observer_,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_NONE));
  EXPECT_CALL(observer_,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_UNKNOWN));
  std::vector<fuchsia::net::Subnet> addresses;
  addresses.push_back(CreateSubnet({0xfe, 0x80, 0x01}, 2));
  netstack_.PushInterface(CreateNetInterface(
      kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
      CreateIPv4Address(169, 254, 0, 1), CreateIPv4Address(255, 255, 255, 0),
      std::move(addresses)));
  netstack_.NotifyInterfaces();
  base::RunLoop().RunUntilIdle();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, InterfaceDown) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  CreateNotifier();
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(observer_,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_NONE));
  netstack_.PushInterface(
      CreateNetInterface(1, 0, 0, CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 0, 0), {}));
  netstack_.NotifyInterfaces();
  base::RunLoop().RunUntilIdle();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, InterfaceUp) {
  netstack_.PushInterface(
      CreateNetInterface(1, 0, 0, CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  CreateNotifier();
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(observer_,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_NONE));
  EXPECT_CALL(observer_,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_UNKNOWN));
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 0, 0), {}));
  netstack_.NotifyInterfaces();
  base::RunLoop().RunUntilIdle();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, InterfaceDeleted) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  CreateNotifier();
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(observer_,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_NONE));
  netstack_.NotifyInterfaces();
  base::RunLoop().RunUntilIdle();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, InterfaceAdded) {
  // Initial interface list is intentionally left empty.
  CreateNotifier();
  base::RunLoop().RunUntilIdle();

  EXPECT_CALL(observer_,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_NONE));
  EXPECT_CALL(observer_,
              OnNetworkChanged(NetworkChangeNotifier::CONNECTION_WIFI));
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp,
                         fuchsia::hardware::ethernet::INFO_FEATURE_WLAN,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.NotifyInterfaces();
  base::RunLoop().RunUntilIdle();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, SecondaryInterfaceAddedNoop) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  CreateNotifier();
  base::RunLoop().RunUntilIdle();

  netstack_.PushInterface(
      CreateNetInterface(kSecondaryNic, fuchsia::netstack::NetInterfaceFlagUp,
                         0, CreateIPv4Address(169, 254, 0, 2),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.NotifyInterfaces();
  base::RunLoop().RunUntilIdle();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, SecondaryInterfaceDeletedNoop) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.PushInterface(
      CreateNetInterface(kSecondaryNic, fuchsia::netstack::NetInterfaceFlagUp,
                         0, CreateIPv4Address(169, 254, 0, 2),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  CreateNotifier();
  base::RunLoop().RunUntilIdle();

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.NotifyInterfaces();
  base::RunLoop().RunUntilIdle();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, FoundWiFi) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp,
                         fuchsia::hardware::ethernet::INFO_FEATURE_WLAN,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  CreateNotifier();
  EXPECT_EQ(NetworkChangeNotifier::ConnectionType::CONNECTION_WIFI,
            notifier_->GetCurrentConnectionType());
}

TEST_F(NetworkChangeNotifierFuchsiaTest, FindsInterfaceWithRequiredFeature) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp,
                         fuchsia::hardware::ethernet::INFO_FEATURE_WLAN,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  CreateNotifier(fuchsia::hardware::ethernet::INFO_FEATURE_WLAN);
  EXPECT_EQ(NetworkChangeNotifier::ConnectionType::CONNECTION_WIFI,
            notifier_->GetCurrentConnectionType());
}

TEST_F(NetworkChangeNotifierFuchsiaTest, IgnoresInterfaceWithMissingFeature) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  CreateNotifier(fuchsia::hardware::ethernet::INFO_FEATURE_WLAN);
  EXPECT_EQ(NetworkChangeNotifier::ConnectionType::CONNECTION_NONE,
            notifier_->GetCurrentConnectionType());
}

}  // namespace net
