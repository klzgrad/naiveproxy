// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/dns/mapped_host_resolver.h"

#include <string>
#include <utility>

#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "net/base/host_port_pair.h"
#include "net/base/net_errors.h"

namespace net {

class MappedHostResolver::AlwaysErrorRequestImpl
    : public HostResolver::ResolveHostRequest {
 public:
  explicit AlwaysErrorRequestImpl(int error) : error_(error) {}

  int Start(CompletionOnceCallback callback) override { return error_; }

  const base::Optional<AddressList>& GetAddressResults() const override {
    static base::NoDestructor<base::Optional<AddressList>> nullopt_result;
    return *nullopt_result;
  }

  const base::Optional<std::vector<std::string>>& GetTextResults()
      const override {
    static const base::NoDestructor<base::Optional<std::vector<std::string>>>
        nullopt_result;
    return *nullopt_result;
  }

  const base::Optional<std::vector<HostPortPair>>& GetHostnameResults()
      const override {
    static const base::NoDestructor<base::Optional<std::vector<HostPortPair>>>
        nullopt_result;
    return *nullopt_result;
  }

  const base::Optional<HostCache::EntryStaleness>& GetStaleInfo()
      const override {
    static const base::NoDestructor<base::Optional<HostCache::EntryStaleness>>
        nullopt_result;
    return *nullopt_result;
  }

 private:
  const int error_;
};

MappedHostResolver::MappedHostResolver(std::unique_ptr<HostResolver> impl)
    : impl_(std::move(impl)) {}

MappedHostResolver::~MappedHostResolver() = default;

std::unique_ptr<HostResolver::ResolveHostRequest>
MappedHostResolver::CreateRequest(
    const HostPortPair& host,
    const NetLogWithSource& source_net_log,
    const base::Optional<ResolveHostParameters>& optional_parameters) {
  HostPortPair rewritten = host;
  rules_.RewriteHost(&rewritten);

  if (rewritten.host() == "~NOTFOUND")
    return std::make_unique<AlwaysErrorRequestImpl>(ERR_NAME_NOT_RESOLVED);

  return impl_->CreateRequest(rewritten, source_net_log, optional_parameters);
}

HostCache* MappedHostResolver::GetHostCache() {
  return impl_->GetHostCache();
}

std::unique_ptr<base::Value> MappedHostResolver::GetDnsConfigAsValue() const {
  return impl_->GetDnsConfigAsValue();
}

void MappedHostResolver::SetRequestContext(URLRequestContext* request_context) {
  impl_->SetRequestContext(request_context);
}

HostResolverManager* MappedHostResolver::GetManagerForTesting() {
  return impl_->GetManagerForTesting();
}

}  // namespace net
