// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DNS_MOJO_HOST_RESOLVER_IMPL_H_
#define NET_DNS_MOJO_HOST_RESOLVER_IMPL_H_

#include <list>
#include <memory>

#include "base/macros.h"
#include "base/threading/thread_checker.h"
#include "net/interfaces/host_resolver_service.mojom.h"
#include "net/log/net_log_with_source.h"

namespace net {

class HostResolver;

// MojoHostResolverImpl handles mojo host resolution requests. Inbound Mojo
// requests are sent to the HostResolver passed into the constructor. When
// destroyed, any outstanding resolver requests are cancelled. If a request's
// HostResolverRequestClient is shut down, the associated resolver request is
// cancelled.
class MojoHostResolverImpl {
 public:
  // |resolver| is expected to outlive |this|.
  MojoHostResolverImpl(net::HostResolver* resolver,
                       const NetLogWithSource& net_log);
  ~MojoHostResolverImpl();

  void Resolve(std::unique_ptr<HostResolver::RequestInfo> request_info,
               interfaces::HostResolverRequestClientPtr client);

  bool request_in_progress() { return !pending_jobs_.empty(); }

 private:
  class Job;

  // Removes |job| from the set of pending jobs.
  void DeleteJob(std::list<Job>::iterator job);

  // Resolver for resolving incoming requests. Not owned.
  net::HostResolver* resolver_;

  // The NetLogWithSource to be passed to |resolver_| for all requests.
  const NetLogWithSource net_log_;

  // All pending jobs, so they can be cancelled when this service is destroyed.
  std::list<Job> pending_jobs_;

  base::ThreadChecker thread_checker_;

  DISALLOW_COPY_AND_ASSIGN(MojoHostResolverImpl);
};

}  // namespace net

#endif  // NET_DNS_MOJO_HOST_RESOLVER_IMPL_H_
