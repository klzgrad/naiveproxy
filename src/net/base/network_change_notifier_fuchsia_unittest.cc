// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/base/network_change_notifier_fuchsia.h"

#include <fuchsia/hardware/ethernet/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl_test_base.h>
#include <memory>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/threading/sequence_bound.h"
#include "base/threading/thread.h"
#include "net/base/ip_address.h"
#include "net/dns/dns_config_service.h"
#include "net/dns/system_dns_config_change_notifier.h"
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
    did_work_ = true;
    binding_.events().OnInterfacesChanged(std::move(interfaces_));
    interfaces_.clear();
  }

  // Sets |*did_work_out| to |true| if any FIDL API was called since the
  // last DidDoWork() call. This is used by the FakeNetstackAsync::Synchronize()
  // call to determine when to stop pumping the message loops.
  void DidDoWork(base::OnceClosure done, bool* did_work_out) {
    *did_work_out = std::exchange(did_work_, false);
    std::move(done).Run();
  }

 private:
  void GetInterfaces(GetInterfacesCallback callback) override {
    did_work_ = true;
    callback(std::move(interfaces_));
  }

  void GetRouteTable(GetRouteTableCallback callback) override {
    did_work_ = true;
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

  // |true| if any FIDL API was called since the last DidDoWork().
  bool did_work_ = false;

  DISALLOW_COPY_AND_ASSIGN(FakeNetstack);
};

class FakeNetstackAsync {
 public:
  explicit FakeNetstackAsync(
      fidl::InterfaceRequest<fuchsia::netstack::Netstack> netstack_request)
      : thread_("Netstack Thread") {
    base::Thread::Options options(base::MessageLoop::TYPE_IO, 0);
    CHECK(thread_.StartWithOptions(options));
    netstack_ = base::SequenceBound<FakeNetstack>(thread_.task_runner(),
                                                  std::move(netstack_request));
  }
  ~FakeNetstackAsync() = default;

  // Asynchronously update the state of the netstack.
  void PushInterface(fuchsia::netstack::NetInterface&& interface) {
    netstack_.Post(FROM_HERE, &FakeNetstack::PushInterface,
                   std::move(interface));
  }
  void PushRouteTableEntry(fuchsia::netstack::RouteTableEntry&& route) {
    netstack_.Post(FROM_HERE, &FakeNetstack::PushRouteTableEntry,
                   std::move(route));
  }
  void NotifyInterfaces() {
    netstack_.Post(FROM_HERE, &FakeNetstack::NotifyInterfaces);
  }

  // Pump the test main and Netstack loops until things stabilize.
  void Synchronize() {
    // Ensure that pending Push*() and Notify*() calls were processed.
    thread_.FlushForTesting();

    // Spin the Netstack until it stops receiving FIDL calls.
    bool did_work = false;
    do {
      base::RunLoop loop;
      did_work = false;
      netstack_.Post(FROM_HERE, &FakeNetstack::DidDoWork,
                     loop.QuitWhenIdleClosure(), &did_work);
      loop.Run();
    } while (did_work);
  }

 private:
  base::Thread thread_;
  base::SequenceBound<FakeNetstack> netstack_;

  DISALLOW_COPY_AND_ASSIGN(FakeNetstackAsync);
};

class MockConnectionTypeObserver
    : public NetworkChangeNotifier::ConnectionTypeObserver {
 public:
  MOCK_METHOD1(OnConnectionTypeChanged,
               void(NetworkChangeNotifier::ConnectionType));
};

class MockIPAddressObserver : public NetworkChangeNotifier::IPAddressObserver {
 public:
  MOCK_METHOD0(OnIPAddressChanged, void());
};

}  // namespace

class NetworkChangeNotifierFuchsiaTest : public testing::Test {
 public:
  NetworkChangeNotifierFuchsiaTest() : netstack_(netstack_ptr_.NewRequest()) {}
  ~NetworkChangeNotifierFuchsiaTest() override = default;

  // Creates a NetworkChangeNotifier and spins the MessageLoop to allow it to
  // populate from the list of interfaces which have already been added to
  // |netstack_|. |observer_| is registered last, so that tests need only
  // express expectations on changes they make themselves.
  void CreateNotifier(uint32_t required_features = 0) {
    // Ensure that the Netstack internal state is up-to-date before the
    // notifier queries it.
    netstack_.Synchronize();

    // Use a noop DNS notifier.
    dns_config_notifier_ = std::make_unique<SystemDnsConfigChangeNotifier>(
        nullptr /* task_runner */, nullptr /* dns_config_service */);
    notifier_.reset(new NetworkChangeNotifierFuchsia(
        std::move(netstack_ptr_), required_features,
        dns_config_notifier_.get()));

    NetworkChangeNotifier::AddConnectionTypeObserver(&observer_);
    NetworkChangeNotifier::AddIPAddressObserver(&ip_observer_);
  }

  void TearDown() override {
    // Spin the loops to catch any unintended notifications.
    netstack_.Synchronize();

    if (notifier_) {
      NetworkChangeNotifier::RemoveConnectionTypeObserver(&observer_);
      NetworkChangeNotifier::RemoveIPAddressObserver(&ip_observer_);
    }
  }

 protected:
  base::MessageLoopForIO message_loop_;
  testing::StrictMock<MockConnectionTypeObserver> observer_;
  testing::StrictMock<MockIPAddressObserver> ip_observer_;
  fuchsia::netstack::NetstackPtr netstack_ptr_;
  FakeNetstackAsync netstack_;

  // Allows us to allocate our own NetworkChangeNotifier for unit testing.
  NetworkChangeNotifier::DisableForTest disable_for_test_;
  std::unique_ptr<SystemDnsConfigChangeNotifier> dns_config_notifier_;
  std::unique_ptr<NetworkChangeNotifierFuchsia> notifier_;

  testing::InSequence seq_;

 private:
  DISALLOW_COPY_AND_ASSIGN(NetworkChangeNotifierFuchsiaTest);
};

TEST_F(NetworkChangeNotifierFuchsiaTest, InitialState) {
  CreateNotifier();
  EXPECT_EQ(NetworkChangeNotifier::ConnectionType::CONNECTION_NONE,
            notifier_->GetCurrentConnectionType());
}

TEST_F(NetworkChangeNotifierFuchsiaTest, NoChange) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.PushRouteTableEntry(CreateRouteTableEntry(kDefaultNic, true));

  CreateNotifier();
  EXPECT_EQ(NetworkChangeNotifier::ConnectionType::CONNECTION_UNKNOWN,
            notifier_->GetCurrentConnectionType());

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.PushRouteTableEntry(CreateRouteTableEntry(kDefaultNic, true));
  netstack_.NotifyInterfaces();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, NoChangeV6) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv6Address({0xfe, 0x80, 0x01}),
                         CreateIPv6Address({0xfe, 0x80}), {}));
  CreateNotifier();

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv6Address({0xfe, 0x80, 0x01}),
                         CreateIPv6Address({0xfe, 0x80}), {}));
  netstack_.NotifyInterfaces();
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

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.PushInterface(
      CreateNetInterface(kSecondaryNic, fuchsia::netstack::NetInterfaceFlagUp,
                         0, CreateIPv4Address(169, 254, 0, 3),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.NotifyInterfaces();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, MultiV6IPNoChange) {
  std::vector<fuchsia::net::Subnet> addresses;
  addresses.push_back(CreateSubnet({0xfe, 0x80, 0x01}, 2));
  netstack_.PushInterface(CreateNetInterface(
      kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
      CreateIPv4Address(169, 254, 0, 1), CreateIPv4Address(255, 255, 255, 0),
      std::move(addresses)));

  CreateNotifier();

  addresses.push_back(CreateSubnet({0xfe, 0x80, 0x01}, 2));
  netstack_.PushInterface(CreateNetInterface(
      kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
      CreateIPv4Address(169, 254, 0, 1), CreateIPv4Address(255, 255, 255, 0),
      std::move(addresses)));
  netstack_.NotifyInterfaces();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, IpChange) {
  EXPECT_CALL(ip_observer_, OnIPAddressChanged());

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  CreateNotifier();

  EXPECT_EQ(NetworkChangeNotifier::ConnectionType::CONNECTION_UNKNOWN,
            notifier_->GetCurrentConnectionType());

  netstack_.PushInterface(CreateNetInterface(
      kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
      CreateIPv4Address(10, 0, 0, 1), CreateIPv4Address(255, 255, 0, 0), {}));
  netstack_.NotifyInterfaces();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, IpChangeV6) {
  EXPECT_CALL(ip_observer_, OnIPAddressChanged());

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv6Address({0xfe, 0x80, 0x01}),
                         CreateIPv6Address({0xfe, 0x80}), {}));
  CreateNotifier();
  EXPECT_EQ(NetworkChangeNotifier::ConnectionType::CONNECTION_UNKNOWN,
            notifier_->GetCurrentConnectionType());

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv6Address({0xfe, 0x80, 0x02}),
                         CreateIPv6Address({0xfe, 0x80}), {}));
  netstack_.NotifyInterfaces();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, MultiV6IPChanged) {
  EXPECT_CALL(ip_observer_, OnIPAddressChanged());

  std::vector<fuchsia::net::Subnet> addresses;
  addresses.push_back(CreateSubnet({0xfe, 0x80, 0x01}, 2));
  netstack_.PushInterface(CreateNetInterface(
      kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
      CreateIPv4Address(169, 254, 0, 1), CreateIPv4Address(255, 255, 255, 0),
      std::move(addresses)));

  CreateNotifier();
  EXPECT_EQ(NetworkChangeNotifier::ConnectionType::CONNECTION_UNKNOWN,
            notifier_->GetCurrentConnectionType());

  addresses.push_back(CreateSubnet({0xfe, 0x80, 0x02}, 2));
  netstack_.PushInterface(CreateNetInterface(
      kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
      CreateIPv4Address(10, 0, 0, 1), CreateIPv4Address(255, 255, 0, 0),
      std::move(addresses)));
  netstack_.NotifyInterfaces();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, Ipv6AdditionalIpChange) {
  EXPECT_CALL(ip_observer_, OnIPAddressChanged());

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));

  CreateNotifier();
  EXPECT_EQ(NetworkChangeNotifier::ConnectionType::CONNECTION_UNKNOWN,
            notifier_->GetCurrentConnectionType());

  std::vector<fuchsia::net::Subnet> addresses;
  addresses.push_back(CreateSubnet({0xfe, 0x80, 0x01}, 2));
  netstack_.PushInterface(CreateNetInterface(
      kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
      CreateIPv4Address(169, 254, 0, 1), CreateIPv4Address(255, 255, 255, 0),
      std::move(addresses)));
  netstack_.NotifyInterfaces();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, InterfaceDown) {
  EXPECT_CALL(ip_observer_, OnIPAddressChanged());
  EXPECT_CALL(observer_,
              OnConnectionTypeChanged(NetworkChangeNotifier::CONNECTION_NONE));

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));

  CreateNotifier();
  EXPECT_EQ(NetworkChangeNotifier::ConnectionType::CONNECTION_UNKNOWN,
            notifier_->GetCurrentConnectionType());

  netstack_.PushInterface(
      CreateNetInterface(1, 0, 0, CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 0, 0), {}));
  netstack_.NotifyInterfaces();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, InterfaceUp) {
  EXPECT_CALL(ip_observer_, OnIPAddressChanged());
  EXPECT_CALL(observer_, OnConnectionTypeChanged(
                             NetworkChangeNotifier::CONNECTION_UNKNOWN));

  netstack_.PushInterface(
      CreateNetInterface(1, 0, 0, CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));

  CreateNotifier();
  EXPECT_EQ(NetworkChangeNotifier::ConnectionType::CONNECTION_NONE,
            notifier_->GetCurrentConnectionType());

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 0, 0), {}));
  netstack_.NotifyInterfaces();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, InterfaceDeleted) {
  EXPECT_CALL(ip_observer_, OnIPAddressChanged());
  EXPECT_CALL(observer_,
              OnConnectionTypeChanged(NetworkChangeNotifier::CONNECTION_NONE));

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  CreateNotifier();
  EXPECT_EQ(NetworkChangeNotifier::ConnectionType::CONNECTION_UNKNOWN,
            notifier_->GetCurrentConnectionType());

  // NotifyInterfaces() with no new PushInterfaces() means removing everything.
  netstack_.NotifyInterfaces();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, InterfaceAdded) {
  EXPECT_CALL(ip_observer_, OnIPAddressChanged());
  EXPECT_CALL(observer_,
              OnConnectionTypeChanged(NetworkChangeNotifier::CONNECTION_WIFI));

  // Initial interface list is intentionally left empty.
  CreateNotifier();

  EXPECT_EQ(NetworkChangeNotifier::ConnectionType::CONNECTION_NONE,
            notifier_->GetCurrentConnectionType());

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp,
                         fuchsia::hardware::ethernet::INFO_FEATURE_WLAN,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.NotifyInterfaces();
}

TEST_F(NetworkChangeNotifierFuchsiaTest, SecondaryInterfaceAddedNoop) {
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  CreateNotifier();

  netstack_.PushInterface(
      CreateNetInterface(kSecondaryNic, fuchsia::netstack::NetInterfaceFlagUp,
                         0, CreateIPv4Address(169, 254, 0, 2),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.NotifyInterfaces();
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

  netstack_.PushInterface(
      CreateNetInterface(kDefaultNic, fuchsia::netstack::NetInterfaceFlagUp, 0,
                         CreateIPv4Address(169, 254, 0, 1),
                         CreateIPv4Address(255, 255, 255, 0), {}));
  netstack_.NotifyInterfaces();
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
