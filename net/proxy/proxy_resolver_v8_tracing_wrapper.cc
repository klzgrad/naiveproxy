// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy/proxy_resolver_v8_tracing_wrapper.h"

#include <string>
#include <utility>

#include "base/bind.h"
#include "base/macros.h"
#include "base/values.h"
#include "net/base/net_errors.h"
#include "net/log/net_log.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_event_type.h"
#include "net/log/net_log_parameters_callback.h"
#include "net/log/net_log_with_source.h"
#include "net/proxy/proxy_resolver_error_observer.h"

namespace net {

namespace {

// Returns event parameters for a PAC error message (line number + message).
std::unique_ptr<base::Value> NetLogErrorCallback(
    int line_number,
    const base::string16* message,
    NetLogCaptureMode /* capture_mode */) {
  std::unique_ptr<base::DictionaryValue> dict(new base::DictionaryValue());
  dict->SetInteger("line_number", line_number);
  dict->SetString("message", *message);
  return std::move(dict);
}

class BindingsImpl : public ProxyResolverV8Tracing::Bindings {
 public:
  BindingsImpl(ProxyResolverErrorObserver* error_observer,
               HostResolver* host_resolver,
               NetLog* net_log,
               const NetLogWithSource& net_log_with_source)
      : error_observer_(error_observer),
        host_resolver_(host_resolver),
        net_log_(net_log),
        net_log_with_source_(net_log_with_source) {}

  // ProxyResolverV8Tracing::Bindings overrides.
  void Alert(const base::string16& message) override {
    // Send to the NetLog.
    LogEventToCurrentRequestAndGlobally(
        NetLogEventType::PAC_JAVASCRIPT_ALERT,
        NetLog::StringCallback("message", &message));
  }

  void OnError(int line_number, const base::string16& message) override {
    // Send the error to the NetLog.
    LogEventToCurrentRequestAndGlobally(
        NetLogEventType::PAC_JAVASCRIPT_ERROR,
        base::Bind(&NetLogErrorCallback, line_number, &message));
    if (error_observer_)
      error_observer_->OnPACScriptError(line_number, message);
  }

  HostResolver* GetHostResolver() override { return host_resolver_; }

  NetLogWithSource GetNetLogWithSource() override {
    return net_log_with_source_;
  }

 private:
  void LogEventToCurrentRequestAndGlobally(
      NetLogEventType type,
      const NetLogParametersCallback& parameters_callback) {
    net_log_with_source_.AddEvent(type, parameters_callback);

    // Emit to the global NetLog event stream.
    if (net_log_)
      net_log_->AddGlobalEntry(type, parameters_callback);
  }

  ProxyResolverErrorObserver* error_observer_;
  HostResolver* host_resolver_;
  NetLog* net_log_;
  NetLogWithSource net_log_with_source_;
};

class ProxyResolverV8TracingWrapper : public ProxyResolver {
 public:
  ProxyResolverV8TracingWrapper(
      std::unique_ptr<ProxyResolverV8Tracing> resolver_impl,
      NetLog* net_log,
      HostResolver* host_resolver,
      std::unique_ptr<ProxyResolverErrorObserver> error_observer);

  int GetProxyForURL(const GURL& url,
                     ProxyInfo* results,
                     const CompletionCallback& callback,
                     std::unique_ptr<Request>* request,
                     const NetLogWithSource& net_log) override;

 private:
  std::unique_ptr<ProxyResolverV8Tracing> resolver_impl_;
  NetLog* net_log_;
  HostResolver* host_resolver_;
  std::unique_ptr<ProxyResolverErrorObserver> error_observer_;

  DISALLOW_COPY_AND_ASSIGN(ProxyResolverV8TracingWrapper);
};

ProxyResolverV8TracingWrapper::ProxyResolverV8TracingWrapper(
    std::unique_ptr<ProxyResolverV8Tracing> resolver_impl,
    NetLog* net_log,
    HostResolver* host_resolver,
    std::unique_ptr<ProxyResolverErrorObserver> error_observer)
    : resolver_impl_(std::move(resolver_impl)),
      net_log_(net_log),
      host_resolver_(host_resolver),
      error_observer_(std::move(error_observer)) {}

int ProxyResolverV8TracingWrapper::GetProxyForURL(
    const GURL& url,
    ProxyInfo* results,
    const CompletionCallback& callback,
    std::unique_ptr<Request>* request,
    const NetLogWithSource& net_log) {
  resolver_impl_->GetProxyForURL(
      url, results, callback, request,
      std::make_unique<BindingsImpl>(error_observer_.get(), host_resolver_,
                                     net_log_, net_log));
  return ERR_IO_PENDING;
}

}  // namespace

ProxyResolverFactoryV8TracingWrapper::ProxyResolverFactoryV8TracingWrapper(
    HostResolver* host_resolver,
    NetLog* net_log,
    const base::Callback<std::unique_ptr<ProxyResolverErrorObserver>()>&
        error_observer_factory)
    : ProxyResolverFactory(true),
      factory_impl_(ProxyResolverV8TracingFactory::Create()),
      host_resolver_(host_resolver),
      net_log_(net_log),
      error_observer_factory_(error_observer_factory) {}

ProxyResolverFactoryV8TracingWrapper::~ProxyResolverFactoryV8TracingWrapper() =
    default;

int ProxyResolverFactoryV8TracingWrapper::CreateProxyResolver(
    const scoped_refptr<ProxyResolverScriptData>& pac_script,
    std::unique_ptr<ProxyResolver>* resolver,
    const CompletionCallback& callback,
    std::unique_ptr<Request>* request) {
  std::unique_ptr<std::unique_ptr<ProxyResolverV8Tracing>> v8_resolver(
      new std::unique_ptr<ProxyResolverV8Tracing>);
  std::unique_ptr<ProxyResolverErrorObserver> error_observer =
      error_observer_factory_.Run();
  // Note: Argument evaluation order is unspecified, so make copies before
  // passing |v8_resolver| and |error_observer|.
  std::unique_ptr<ProxyResolverV8Tracing>* v8_resolver_local =
      v8_resolver.get();
  ProxyResolverErrorObserver* error_observer_local = error_observer.get();
  factory_impl_->CreateProxyResolverV8Tracing(
      pac_script,
      std::make_unique<BindingsImpl>(error_observer_local, host_resolver_,
                                     net_log_, NetLogWithSource()),
      v8_resolver_local,
      base::Bind(&ProxyResolverFactoryV8TracingWrapper::OnProxyResolverCreated,
                 base::Unretained(this), base::Passed(&v8_resolver), resolver,
                 callback, base::Passed(&error_observer)),
      request);
  return ERR_IO_PENDING;
}

void ProxyResolverFactoryV8TracingWrapper::OnProxyResolverCreated(
    std::unique_ptr<std::unique_ptr<ProxyResolverV8Tracing>> v8_resolver,
    std::unique_ptr<ProxyResolver>* resolver,
    const CompletionCallback& callback,
    std::unique_ptr<ProxyResolverErrorObserver> error_observer,
    int error) {
  if (error == OK) {
    resolver->reset(new ProxyResolverV8TracingWrapper(
        std::move(*v8_resolver), net_log_, host_resolver_,
        std::move(error_observer)));
  }
  callback.Run(error);
}

}  // namespace net
