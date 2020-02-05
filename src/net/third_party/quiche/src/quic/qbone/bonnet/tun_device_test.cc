// Copyright (c) 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/third_party/quiche/src/quic/qbone/bonnet/tun_device.h"

#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>

#include "net/third_party/quiche/src/quic/platform/api/quic_test.h"
#include "net/third_party/quiche/src/quic/qbone/platform/mock_kernel.h"

namespace quic {
namespace {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::Unused;

const char kDeviceName[] = "tun0";
const int kSupportedFeatures =
    IFF_TUN | IFF_TAP | IFF_MULTI_QUEUE | IFF_ONE_QUEUE | IFF_NO_PI;

// Quite a bit of EXPECT_CALL().Times(AnyNumber()).WillRepeatedly() are used to
// make sure we can correctly set common expectations and override the
// expectation with later call to EXPECT_CALL(). ON_CALL cannot be used here
// since when EPXECT_CALL overrides ON_CALL, it ignores the parameter matcher
// which results in unexpected call even if ON_CALL exists.
class TunDeviceTest : public QuicTest {
 protected:
  void SetUp() override {
    EXPECT_CALL(mock_kernel_, socket(AF_INET6, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([this](Unused, Unused, Unused) {
          EXPECT_CALL(mock_kernel_, close(next_fd_)).WillOnce(Return(0));
          return next_fd_++;
        }));
  }

  // Set the expectations for calling Init().
  void SetInitExpectations(int mtu, bool persist) {
    EXPECT_CALL(mock_kernel_, open(StrEq("/dev/net/tun"), _))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([this](Unused, Unused) {
          EXPECT_CALL(mock_kernel_, close(next_fd_)).WillOnce(Return(0));
          return next_fd_++;
        }));
    EXPECT_CALL(mock_kernel_, ioctl(_, TUNGETFEATURES, _))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([](Unused, Unused, void* argp) {
          auto* actual_flags = reinterpret_cast<int*>(argp);
          *actual_flags = kSupportedFeatures;
          return 0;
        }));
    EXPECT_CALL(mock_kernel_, ioctl(_, TUNSETIFF, _))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([](Unused, Unused, void* argp) {
          auto* ifr = reinterpret_cast<struct ifreq*>(argp);
          EXPECT_EQ(IFF_TUN | IFF_MULTI_QUEUE | IFF_NO_PI, ifr->ifr_flags);
          EXPECT_THAT(ifr->ifr_name, StrEq(kDeviceName));
          return 0;
        }));
    EXPECT_CALL(mock_kernel_, ioctl(_, TUNSETPERSIST, _))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([persist](Unused, Unused, void* argp) {
          auto* ifr = reinterpret_cast<struct ifreq*>(argp);
          if (persist) {
            EXPECT_THAT(ifr->ifr_name, StrEq(kDeviceName));
          } else {
            EXPECT_EQ(nullptr, ifr);
          }
          return 0;
        }));
    EXPECT_CALL(mock_kernel_, ioctl(_, SIOCSIFMTU, _))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([mtu](Unused, Unused, void* argp) {
          auto* ifr = reinterpret_cast<struct ifreq*>(argp);
          EXPECT_EQ(mtu, ifr->ifr_mtu);
          EXPECT_THAT(ifr->ifr_name, StrEq(kDeviceName));
          return 0;
        }));
  }

  // Expect that Up() will be called. Force the call to fail when fail == true.
  void ExpectUp(bool fail) {
    EXPECT_CALL(mock_kernel_, ioctl(_, SIOCSIFFLAGS, _))
        .WillOnce(Invoke([fail](Unused, Unused, void* argp) {
          auto* ifr = reinterpret_cast<struct ifreq*>(argp);
          EXPECT_TRUE(ifr->ifr_flags & IFF_UP);
          EXPECT_THAT(ifr->ifr_name, StrEq(kDeviceName));
          if (fail) {
            return -1;
          } else {
            return 0;
          }
        }));
  }

  // Expect that Down() will be called *after* the interface is up. Force the
  // call to fail when fail == true.
  void ExpectDown(bool fail) {
    EXPECT_CALL(mock_kernel_, ioctl(_, SIOCSIFFLAGS, _))
        .WillOnce(Invoke([fail](Unused, Unused, void* argp) {
          auto* ifr = reinterpret_cast<struct ifreq*>(argp);
          EXPECT_FALSE(ifr->ifr_flags & IFF_UP);
          EXPECT_THAT(ifr->ifr_name, StrEq(kDeviceName));
          if (fail) {
            return -1;
          } else {
            return 0;
          }
        }));
  }

  MockKernel mock_kernel_;
  int next_fd_ = 100;
};

// A TunDevice can be initialized and up
TEST_F(TunDeviceTest, BasicWorkFlow) {
  SetInitExpectations(/* mtu = */ 1500, /* persist = */ false);
  TunDevice tun_device(kDeviceName, 1500, false, &mock_kernel_);
  EXPECT_TRUE(tun_device.Init());
  EXPECT_GT(tun_device.GetFileDescriptor(), -1);

  ExpectUp(/* fail = */ false);
  EXPECT_TRUE(tun_device.Up());
  ExpectDown(/* fail = */ false);
}

TEST_F(TunDeviceTest, FailToOpenTunDevice) {
  SetInitExpectations(/* mtu = */ 1500, /* persist = */ false);
  EXPECT_CALL(mock_kernel_, open(StrEq("/dev/net/tun"), _))
      .WillOnce(Return(-1));
  TunDevice tun_device(kDeviceName, 1500, false, &mock_kernel_);
  EXPECT_FALSE(tun_device.Init());
  EXPECT_EQ(tun_device.GetFileDescriptor(), -1);
}

TEST_F(TunDeviceTest, FailToCheckFeature) {
  SetInitExpectations(/* mtu = */ 1500, /* persist = */ false);
  EXPECT_CALL(mock_kernel_, ioctl(_, TUNGETFEATURES, _)).WillOnce(Return(-1));
  TunDevice tun_device(kDeviceName, 1500, false, &mock_kernel_);
  EXPECT_FALSE(tun_device.Init());
  EXPECT_EQ(tun_device.GetFileDescriptor(), -1);
}

TEST_F(TunDeviceTest, TooFewFeature) {
  SetInitExpectations(/* mtu = */ 1500, /* persist = */ false);
  EXPECT_CALL(mock_kernel_, ioctl(_, TUNGETFEATURES, _))
      .WillOnce(Invoke([](Unused, Unused, void* argp) {
        int* actual_features = reinterpret_cast<int*>(argp);
        *actual_features = IFF_TUN | IFF_ONE_QUEUE;
        return 0;
      }));
  TunDevice tun_device(kDeviceName, 1500, false, &mock_kernel_);
  EXPECT_FALSE(tun_device.Init());
  EXPECT_EQ(tun_device.GetFileDescriptor(), -1);
}

TEST_F(TunDeviceTest, FailToSetFlag) {
  SetInitExpectations(/* mtu = */ 1500, /* persist = */ true);
  EXPECT_CALL(mock_kernel_, ioctl(_, TUNSETIFF, _)).WillOnce(Return(-1));
  TunDevice tun_device(kDeviceName, 1500, true, &mock_kernel_);
  EXPECT_FALSE(tun_device.Init());
  EXPECT_EQ(tun_device.GetFileDescriptor(), -1);
}

TEST_F(TunDeviceTest, FailToPersistDevice) {
  SetInitExpectations(/* mtu = */ 1500, /* persist = */ true);
  EXPECT_CALL(mock_kernel_, ioctl(_, TUNSETPERSIST, _)).WillOnce(Return(-1));
  TunDevice tun_device(kDeviceName, 1500, true, &mock_kernel_);
  EXPECT_FALSE(tun_device.Init());
  EXPECT_EQ(tun_device.GetFileDescriptor(), -1);
}

TEST_F(TunDeviceTest, FailToOpenSocket) {
  SetInitExpectations(/* mtu = */ 1500, /* persist = */ true);
  EXPECT_CALL(mock_kernel_, socket(AF_INET6, _, _)).WillOnce(Return(-1));
  TunDevice tun_device(kDeviceName, 1500, true, &mock_kernel_);
  EXPECT_FALSE(tun_device.Init());
  EXPECT_EQ(tun_device.GetFileDescriptor(), -1);
}

TEST_F(TunDeviceTest, FailToSetMtu) {
  SetInitExpectations(/* mtu = */ 1500, /* persist = */ true);
  EXPECT_CALL(mock_kernel_, ioctl(_, SIOCSIFMTU, _)).WillOnce(Return(-1));
  TunDevice tun_device(kDeviceName, 1500, true, &mock_kernel_);
  EXPECT_FALSE(tun_device.Init());
  EXPECT_EQ(tun_device.GetFileDescriptor(), -1);
}

TEST_F(TunDeviceTest, FailToUp) {
  SetInitExpectations(/* mtu = */ 1500, /* persist = */ true);
  TunDevice tun_device(kDeviceName, 1500, true, &mock_kernel_);
  EXPECT_TRUE(tun_device.Init());
  EXPECT_GT(tun_device.GetFileDescriptor(), -1);

  ExpectUp(/* fail = */ true);
  EXPECT_FALSE(tun_device.Up());
}

}  // namespace
}  // namespace quic
