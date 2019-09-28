// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FUCHSIA_SCOPED_SERVICE_BINDING_H_
#define BASE_FUCHSIA_SCOPED_SERVICE_BINDING_H_

#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/outgoing_directory.h>

#include "base/bind.h"
#include "base/callback.h"
#include "base/fuchsia/service_directory.h"

namespace base {
namespace fuchsia {

template <typename Interface>
class ScopedServiceBinding {
 public:
  // Published a public service in the specified |outgoing_directory|.
  // |outgoing_directory| and |impl| must outlive the binding.
  ScopedServiceBinding(sys::OutgoingDirectory* outgoing_directory,
                       Interface* impl)
      : directory_(outgoing_directory), impl_(impl) {
    directory_->AddPublicService<Interface>(
        [this](fidl::InterfaceRequest<Interface> request) {
          BindClient(std::move(request));
        });
  }

  // Publishes a service in the specified |pseudo_dir|. |pseudo_dir| and |impl|
  // must outlive the binding.
  ScopedServiceBinding(vfs::PseudoDir* pseudo_dir, Interface* impl)
      : pseudo_dir_(pseudo_dir), impl_(impl) {
    pseudo_dir_->AddEntry(
        Interface::Name_,
        std::make_unique<vfs::Service>(fidl::InterfaceRequestHandler<Interface>(
            [this](fidl::InterfaceRequest<Interface> request) {
              BindClient(std::move(request));
            })));
  }

  // TODO(crbug.com/974072): Remove this constructor once all code has been
  // migrated from base::fuchsia::ServiceDirectory to sys::OutgoingDirectory.
  ScopedServiceBinding(ServiceDirectory* service_directory, Interface* impl)
      : ScopedServiceBinding(service_directory->outgoing_directory(), impl) {}

  ~ScopedServiceBinding() {
    if (directory_) {
      directory_->RemovePublicService<Interface>();
    } else {
      pseudo_dir_->RemoveEntry(Interface::Name_);
    }
  }

  void SetOnLastClientCallback(base::OnceClosure on_last_client_callback) {
    on_last_client_callback_ = std::move(on_last_client_callback);
    bindings_.set_empty_set_handler(
        fit::bind_member(this, &ScopedServiceBinding::OnBindingSetEmpty));
  }

  bool has_clients() const { return bindings_.size() != 0; }

 private:
  void BindClient(fidl::InterfaceRequest<Interface> request) {
    bindings_.AddBinding(impl_, std::move(request));
  }

  void OnBindingSetEmpty() {
    bindings_.set_empty_set_handler(nullptr);
    std::move(on_last_client_callback_).Run();
  }

  sys::OutgoingDirectory* const directory_ = nullptr;
  vfs::PseudoDir* const pseudo_dir_ = nullptr;
  Interface* const impl_;
  fidl::BindingSet<Interface> bindings_;
  base::OnceClosure on_last_client_callback_;

  DISALLOW_COPY_AND_ASSIGN(ScopedServiceBinding);
};

// Scoped service binding which allows only a single client to be connected
// at any time. By default a new connection will disconnect an existing client.
enum class ScopedServiceBindingPolicy { kPreferNew, kPreferExisting };

template <typename Interface,
          ScopedServiceBindingPolicy Policy =
              ScopedServiceBindingPolicy::kPreferNew>
class ScopedSingleClientServiceBinding {
 public:
  // |outgoing_directory| and |impl| must outlive the binding.
  ScopedSingleClientServiceBinding(sys::OutgoingDirectory* outgoing_directory,
                                   Interface* impl)
      : directory_(std::move(outgoing_directory)), binding_(impl) {
    directory_->AddPublicService<Interface>(
        [this](fidl::InterfaceRequest<Interface> request) {
          BindClient(std::move(request));
        });
  }

  // TODO(crbug.com/974072): Remove this constructor once all code has been
  // migrated from base::fuchsia::ServiceDirectory to sys::OutgoingDirectory.
  ScopedSingleClientServiceBinding(ServiceDirectory* service_directory,
                                   Interface* impl)
      : ScopedSingleClientServiceBinding(
            service_directory->outgoing_directory(),
            impl) {}

  ~ScopedSingleClientServiceBinding() {
    directory_->RemovePublicService<Interface>();
  }

  typename Interface::EventSender_& events() { return binding_.events(); }

  void SetOnLastClientCallback(base::OnceClosure on_last_client_callback) {
    on_last_client_callback_ = std::move(on_last_client_callback);
    binding_.set_error_handler(fit::bind_member(
        this, &ScopedSingleClientServiceBinding::OnBindingEmpty));
  }

  bool has_clients() const { return binding_.is_bound(); }

 private:
  void BindClient(fidl::InterfaceRequest<Interface> request) {
    if (Policy == ScopedServiceBindingPolicy::kPreferExisting &&
        binding_.is_bound()) {
      return;
    }
    binding_.Bind(std::move(request));
  }

  void OnBindingEmpty() {
    binding_.set_error_handler(nullptr);
    std::move(on_last_client_callback_).Run();
  }

  sys::OutgoingDirectory* const directory_;
  fidl::Binding<Interface> binding_;
  base::OnceClosure on_last_client_callback_;

  DISALLOW_COPY_AND_ASSIGN(ScopedSingleClientServiceBinding);
};

}  // namespace fuchsia
}  // namespace base

#endif  // BASE_FUCHSIA_SCOPED_SERVICE_BINDING_H_
