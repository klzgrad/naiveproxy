// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/mojo_host_resolver_impl.h"

#include <utility>

#include "net/base/address_list.h"
#include "net/base/net_errors.h"
#include "net/base/network_interfaces.h"
#include "net/dns/host_resolver.h"

namespace net {

// Handles host resolution for a single request and sends a response when done.
// Also detects connection errors for HostResolverRequestClient and cancels the
// outstanding resolve request. Owned by MojoHostResolverImpl.
class MojoHostResolverImpl::Job {
 public:
  Job(MojoHostResolverImpl* resolver_service,
      net::HostResolver* resolver,
      const net::HostResolver::RequestInfo& request_info,
      const NetLogWithSource& net_log,
      interfaces::HostResolverRequestClientPtr client);
  ~Job();

  void set_iter(std::list<Job>::iterator iter) { iter_ = iter; }

  void Start();

 private:
  // Completion callback for the HostResolver::Resolve request.
  void OnResolveDone(int result);

  // Mojo error handler.
  void OnConnectionError();

  MojoHostResolverImpl* resolver_service_;
  // This Job's iterator in |resolver_service_|, so the Job may be removed on
  // completion.
  std::list<Job>::iterator iter_;
  net::HostResolver* resolver_;
  net::HostResolver::RequestInfo request_info_;
  const NetLogWithSource net_log_;
  interfaces::HostResolverRequestClientPtr client_;
  std::unique_ptr<net::HostResolver::Request> request_;
  AddressList result_;
  base::ThreadChecker thread_checker_;
};

MojoHostResolverImpl::MojoHostResolverImpl(net::HostResolver* resolver,
                                           const NetLogWithSource& net_log)
    : resolver_(resolver), net_log_(net_log) {}

MojoHostResolverImpl::~MojoHostResolverImpl() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void MojoHostResolverImpl::Resolve(
    std::unique_ptr<HostResolver::RequestInfo> request_info,
    interfaces::HostResolverRequestClientPtr client) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (request_info->is_my_ip_address()) {
    // The proxy resolver running inside a sandbox may not be able to get the
    // correct host name. Instead, fill it ourself if the request is for our own
    // IP address.
    request_info->set_host_port_pair(HostPortPair(GetHostName(), 80));
  }

  pending_jobs_.emplace_front(this, resolver_, *request_info, net_log_,
                              std::move(client));
  auto job = pending_jobs_.begin();
  job->set_iter(job);
  job->Start();
}

void MojoHostResolverImpl::DeleteJob(std::list<Job>::iterator job) {
  DCHECK(thread_checker_.CalledOnValidThread());
  pending_jobs_.erase(job);
}

MojoHostResolverImpl::Job::Job(
    MojoHostResolverImpl* resolver_service,
    net::HostResolver* resolver,
    const net::HostResolver::RequestInfo& request_info,
    const NetLogWithSource& net_log,
    interfaces::HostResolverRequestClientPtr client)
    : resolver_service_(resolver_service),
      resolver_(resolver),
      request_info_(request_info),
      net_log_(net_log),
      client_(std::move(client)) {
  client_.set_connection_error_handler(base::Bind(
      &MojoHostResolverImpl::Job::OnConnectionError, base::Unretained(this)));
}

void MojoHostResolverImpl::Job::Start() {
  // The caller is responsible for setting up |iter_|.
  DCHECK_EQ(this, &*iter_);

  DVLOG(1) << "Resolve " << request_info_.host_port_pair().ToString();
  int result =
      resolver_->Resolve(request_info_, DEFAULT_PRIORITY, &result_,
                         base::Bind(&MojoHostResolverImpl::Job::OnResolveDone,
                                    base::Unretained(this)),
                         &request_, net_log_);

  if (result != ERR_IO_PENDING)
    OnResolveDone(result);
}

MojoHostResolverImpl::Job::~Job() = default;

void MojoHostResolverImpl::Job::OnResolveDone(int result) {
  DCHECK(thread_checker_.CalledOnValidThread());
  request_.reset();
  DVLOG(1) << "Resolved " << request_info_.host_port_pair().ToString()
           << " with error " << result << " and " << result_.size()
           << " results!";
  for (const auto& address : result_) {
    DVLOG(1) << address.ToString();
  }
  client_->ReportResult(result, result_);
  resolver_service_->DeleteJob(iter_);
}

void MojoHostResolverImpl::Job::OnConnectionError() {
  DCHECK(thread_checker_.CalledOnValidThread());
  // |resolver_service_| should always outlive us.
  DCHECK(resolver_service_);
  DVLOG(1) << "Connection error on request for "
           << request_info_.host_port_pair().ToString();
  resolver_service_->DeleteJob(iter_);
}

}  // namespace net
