// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/service_provider_impl.h"

#include <utility>

namespace base {
namespace fuchsia {

ServiceProviderImpl::ServiceProviderImpl(
    fidl::InterfaceHandle<::fuchsia::io::Directory> service_directory)
    : directory_(std::move(service_directory)) {}

ServiceProviderImpl::~ServiceProviderImpl() = default;

void ServiceProviderImpl::AddBinding(
    fidl::InterfaceRequest<::fuchsia::sys::ServiceProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ServiceProviderImpl::ConnectToService(std::string service_name,
                                           zx::channel client_handle) {
  directory_.ConnectToServiceUnsafe(service_name.c_str(),
                                    std::move(client_handle));
}

void ServiceProviderImpl::SetOnLastClientDisconnectedClosure(
    base::OnceClosure on_last_client_disconnected) {
  on_last_client_disconnected_ = std::move(on_last_client_disconnected);
  bindings_.set_empty_set_handler(
      fit::bind_member(this, &ServiceProviderImpl::OnBindingSetEmpty));
}

void ServiceProviderImpl::OnBindingSetEmpty() {
  bindings_.set_empty_set_handler(nullptr);
  std::move(on_last_client_disconnected_).Run();
}

}  // namespace fuchsia
}  // namespace base
