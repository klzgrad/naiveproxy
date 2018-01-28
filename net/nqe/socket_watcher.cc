// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/nqe/socket_watcher.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/single_thread_task_runner.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "net/base/address_list.h"
#include "net/base/ip_address.h"

namespace net {

namespace nqe {

namespace internal {

namespace {

// Generate a compact representation for the first IP in |address_list|. For
// IPv4, all 32 bits are used and for IPv6, the first 64 bits are used as the
// remote host identifier.
base::Optional<IPHash> CalculateIPHash(const AddressList& address_list) {
  if (address_list.empty())
    return base::nullopt;

  const IPAddress& ip_addr = address_list.front().address();

  IPAddressBytes bytes = ip_addr.bytes();

  // For IPv4, the first four bytes are taken. For IPv6, the first 8 bytes are
  // taken. For IPv4MappedIPv6, the last 4 bytes are taken.
  int index_min = ip_addr.IsIPv4MappedIPv6() ? 12 : 0;
  int index_max;
  if (ip_addr.IsIPv4MappedIPv6())
    index_max = 16;
  else
    index_max = ip_addr.IsIPv4() ? 4 : 8;

  DCHECK_LE(index_min, index_max);
  DCHECK_GE(8, index_max - index_min);

  uint64_t result = 0ULL;
  for (int i = index_min; i < index_max; ++i) {
    result = result << 8;
    result |= bytes[i];
  }
  return result;
}

}  // namespace

SocketWatcher::SocketWatcher(
    SocketPerformanceWatcherFactory::Protocol protocol,
    const AddressList& address_list,
    base::TimeDelta min_notification_interval,
    bool allow_rtt_private_address,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    OnUpdatedRTTAvailableCallback updated_rtt_observation_callback,
    base::TickClock* tick_clock)
    : protocol_(protocol),
      task_runner_(std::move(task_runner)),
      updated_rtt_observation_callback_(updated_rtt_observation_callback),
      rtt_notifications_minimum_interval_(min_notification_interval),
      run_rtt_callback_(allow_rtt_private_address ||
                        (!address_list.empty() &&
                         !address_list.front().address().IsReserved())),
      tick_clock_(tick_clock),
      host_(CalculateIPHash(address_list)) {
  DCHECK(tick_clock_);
}

SocketWatcher::~SocketWatcher() {}

bool SocketWatcher::ShouldNotifyUpdatedRTT() const {
  DCHECK(thread_checker_.CalledOnValidThread());

  // Do not allow incoming notifications if the last notification was more
  // recent than |rtt_notifications_minimum_interval_| ago. This helps in
  // reducing the overhead of obtaining the RTT values.
  return run_rtt_callback_ &&
         tick_clock_->NowTicks() - last_rtt_notification_ >=
             rtt_notifications_minimum_interval_;
}

void SocketWatcher::OnUpdatedRTTAvailable(const base::TimeDelta& rtt) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (rtt <= base::TimeDelta())
    return;

  last_rtt_notification_ = tick_clock_->NowTicks();
  task_runner_->PostTask(
      FROM_HERE,
      base::Bind(updated_rtt_observation_callback_, protocol_, rtt, host_));
}

void SocketWatcher::OnConnectionChanged() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

}  // namespace internal

}  // namespace nqe

}  // namespace net
