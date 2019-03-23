// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/tools/quic/synchronous_host_resolver.h"

#include "base/at_exit.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/simple_thread.h"
#include "base/threading/thread_task_runner_handle.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_errors.h"
#include "net/dns/host_resolver_impl.h"
#include "net/log/net_log.h"
#include "net/log/net_log_with_source.h"

namespace net {


namespace {

class ResolverThread : public base::SimpleThread {
 public:
  ResolverThread();

  ~ResolverThread() override;

  // Called on the main thread.
  int Resolve(const std::string& host, AddressList* addresses);

  // SimpleThread methods:
  void Run() override;

 private:
  void OnResolutionComplete(base::OnceClosure on_done, int rv);

  AddressList* addresses_;
  std::string host_;
  int rv_;

  DISALLOW_COPY_AND_ASSIGN(ResolverThread);
};

ResolverThread::ResolverThread()
    : SimpleThread("resolver_thread"), rv_(ERR_UNEXPECTED) {}

ResolverThread::~ResolverThread() = default;

void ResolverThread::Run() {
  base::MessageLoopForIO loop;

  net::NetLog net_log;
  net::HostResolver::Options options;
  options.max_concurrent_resolves = 6;
  options.max_retry_attempts = 3u;
  std::unique_ptr<net::HostResolverImpl> resolver(
      new net::HostResolverImpl(options, &net_log));

  std::unique_ptr<net::HostResolver::Request> request;
  HostPortPair host_port_pair(host_, 80);
  base::RunLoop run_loop;
  rv_ = resolver->Resolve(
      HostResolver::RequestInfo(host_port_pair), DEFAULT_PRIORITY, addresses_,
      base::Bind(&ResolverThread::OnResolutionComplete, base::Unretained(this),
                 run_loop.QuitClosure()),
      &request, NetLogWithSource());

  if (rv_ != ERR_IO_PENDING)
    return;

  // Run the mesage loop until OnResolutionComplete quits it.
  run_loop.Run();
}

int ResolverThread::Resolve(const std::string& host, AddressList* addresses) {
  host_ = host;
  addresses_ = addresses;
  this->Start();
  this->Join();
  return rv_;
}

void ResolverThread::OnResolutionComplete(base::OnceClosure on_done, int rv) {
  rv_ = rv;
  std::move(on_done).Run();
}

}  // namespace

// static
int SynchronousHostResolver::Resolve(const std::string& host,
                                     AddressList* addresses) {
  ResolverThread resolver;
  return resolver.Resolve(host, addresses);
}

}  // namespace net
