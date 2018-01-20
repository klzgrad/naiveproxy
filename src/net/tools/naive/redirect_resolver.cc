// Copyright 2019 klzgrad <kizdiv@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/naive/redirect_resolver.h"

#include <cstring>
#include <iterator>
#include <utility>

#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "net/dns/dns_names_util.h"
#include "net/dns/dns_query.h"
#include "net/dns/dns_response.h"
#include "net/dns/dns_util.h"
#include "net/socket/datagram_server_socket.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace {
constexpr int kUdpReadBufferSize = 1024;
constexpr int kResolutionTtl = 60;
constexpr int kResolutionRecycleTime = 60 * 5;

std::string PackedIPv4ToString(uint32_t addr) {
  return net::IPAddress(addr >> 24, addr >> 16, addr >> 8, addr).ToString();
}
}  // namespace

namespace net {

Resolution::Resolution() = default;

Resolution::~Resolution() = default;

RedirectResolver::RedirectResolver(std::unique_ptr<DatagramServerSocket> socket,
                                   const IPAddress& range,
                                   size_t prefix)
    : socket_(std::move(socket)),
      range_(range),
      prefix_(prefix),
      offset_(0),
      buffer_(base::MakeRefCounted<IOBufferWithSize>(kUdpReadBufferSize)) {
  DCHECK(socket_);
  // Start accepting connections in next run loop in case when delegate is not
  // ready to get callbacks.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&RedirectResolver::DoRead,
                                weak_ptr_factory_.GetWeakPtr()));
}

RedirectResolver::~RedirectResolver() = default;

void RedirectResolver::DoRead() {
  for (;;) {
    int rv = socket_->RecvFrom(
        buffer_.get(), kUdpReadBufferSize, &recv_address_,
        base::BindOnce(&RedirectResolver::OnRecv, base::Unretained(this)));
    if (rv == ERR_IO_PENDING)
      return;
    rv = HandleReadResult(rv);
    if (rv == ERR_IO_PENDING)
      return;
    if (rv < 0) {
      LOG(INFO) << "DoRead: ignoring error " << rv;
    }
  }
}

void RedirectResolver::OnRecv(int result) {
  int rv;
  rv = HandleReadResult(result);
  if (rv == ERR_IO_PENDING)
    return;
  if (rv < 0) {
    LOG(INFO) << "OnRecv: ignoring error " << result;
  }

  DoRead();
}

void RedirectResolver::OnSend(int result) {
  if (result < 0) {
    LOG(INFO) << "OnSend: ignoring error " << result;
  }

  DoRead();
}

int RedirectResolver::HandleReadResult(int result) {
  if (result < 0)
    return result;

  DnsQuery query(buffer_.get());
  if (!query.Parse(result)) {
    LOG(INFO) << "Malformed DNS query from " << recv_address_.ToString();
    return ERR_INVALID_ARGUMENT;
  }

  auto name_or = dns_names_util::NetworkToDottedName(query.qname());
  DnsResponse response;
  absl::optional<DnsQuery> query_opt;
  query_opt.emplace(query.id(), query.qname(), query.qtype());
  if (!name_or || !IsCanonicalizedHostCompliant(name_or.value())) {
    response =
        DnsResponse(query.id(), /*is_authoritative=*/false, /*answers=*/{},
                    /*authority_records=*/{}, /*additional_records=*/{},
                    query_opt, dns_protocol::kRcodeFORMERR);
  } else if (query.qtype() != dns_protocol::kTypeA) {
    response =
        DnsResponse(query.id(), /*is_authoritative=*/false, /*answers=*/{},
                    /*authority_records=*/{}, /*additional_records=*/{},
                    query_opt, dns_protocol::kRcodeNOTIMP);
  } else {
    Resolution res;

    const auto& name = name_or.value();

    auto by_name_lookup = resolution_by_name_.emplace(name, resolutions_.end());
    auto by_name = by_name_lookup.first;
    bool has_name = !by_name_lookup.second;
    if (has_name) {
      auto res_it = by_name->second;
      auto by_addr = res_it->by_addr;
      uint32_t addr = res_it->addr;

      resolutions_.erase(res_it);
      resolutions_.emplace_back();
      res_it = std::prev(resolutions_.end());

      by_name->second = res_it;
      by_addr->second = res_it;
      res_it->addr = addr;
      res_it->name = name;
      res_it->time = base::TimeTicks::Now();
      res_it->by_name = by_name;
      res_it->by_addr = by_addr;
    } else {
      uint32_t addr = (range_.bytes()[0] << 24) | (range_.bytes()[1] << 16) |
                      (range_.bytes()[2] << 8) | range_.bytes()[3];
      uint32_t subnet = ~0U >> prefix_;
      addr &= ~subnet;
      addr += offset_;
      offset_ = (offset_ + 1) & subnet;

      auto by_addr_lookup =
          resolution_by_addr_.emplace(addr, resolutions_.end());
      auto by_addr = by_addr_lookup.first;
      bool has_addr = !by_addr_lookup.second;
      if (has_addr) {
        // Too few available addresses. Overwrites old one.
        auto res_it = by_addr->second;

        LOG(INFO) << "Overwrite " << res_it->name << " "
                  << PackedIPv4ToString(res_it->addr) << " with " << name << " "
                  << PackedIPv4ToString(addr);
        resolution_by_name_.erase(res_it->by_name);
        resolutions_.erase(res_it);
        resolutions_.emplace_back();
        res_it = std::prev(resolutions_.end());

        by_name->second = res_it;
        by_addr->second = res_it;
        res_it->addr = addr;
        res_it->name = name;
        res_it->time = base::TimeTicks::Now();
        res_it->by_name = by_name;
        res_it->by_addr = by_addr;
      } else {
        LOG(INFO) << "Add " << name << " " << PackedIPv4ToString(addr);
        resolutions_.emplace_back();
        auto res_it = std::prev(resolutions_.end());

        by_name->second = res_it;
        by_addr->second = res_it;
        res_it->addr = addr;
        res_it->name = name;
        res_it->time = base::TimeTicks::Now();
        res_it->by_name = by_name;
        res_it->by_addr = by_addr;

        // Collects garbage.
        auto now = base::TimeTicks::Now();
        for (auto it = resolutions_.begin();
             it != resolutions_.end() &&
             (now - it->time).InSeconds() > kResolutionRecycleTime;) {
          auto next = std::next(it);
          LOG(INFO) << "Drop " << it->name << " "
                    << PackedIPv4ToString(it->addr);
          resolution_by_name_.erase(it->by_name);
          resolution_by_addr_.erase(it->by_addr);
          resolutions_.erase(it);
          it = next;
        }
      }
    }

    DnsResourceRecord record;
    record.name = name;
    record.type = dns_protocol::kTypeA;
    record.klass = dns_protocol::kClassIN;
    record.ttl = kResolutionTtl;
    uint32_t addr = by_name->second->addr;
    record.SetOwnedRdata(IPAddressToPackedString(
        IPAddress(addr >> 24, addr >> 16, addr >> 8, addr)));
    response = DnsResponse(query.id(), /*is_authoritative=*/false,
                           /*answers=*/{std::move(record)},
                           /*authority_records=*/{}, /*additional_records=*/{},
                           query_opt);
  }
  int size = response.io_buffer_size();
  if (size > buffer_->size() || !response.io_buffer()) {
    return ERR_NO_BUFFER_SPACE;
  }
  std::memcpy(buffer_->data(), response.io_buffer()->data(), size);

  return socket_->SendTo(
      buffer_.get(), size, recv_address_,
      base::BindOnce(&RedirectResolver::OnSend, base::Unretained(this)));
}

bool RedirectResolver::IsInResolvedRange(const IPAddress& address) const {
  if (!address.IsIPv4())
    return false;
  return IPAddressMatchesPrefix(address, range_, prefix_);
}

std::string RedirectResolver::FindNameByAddress(
    const IPAddress& address) const {
  if (!address.IsIPv4())
    return {};
  uint32_t addr = (address.bytes()[0] << 24) | (address.bytes()[1] << 16) |
                  (address.bytes()[2] << 8) | address.bytes()[3];
  auto by_addr = resolution_by_addr_.find(addr);
  if (by_addr == resolution_by_addr_.end())
    return {};
  return by_addr->second->name;
}

}  // namespace net
