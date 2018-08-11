// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FUCHSIA_SCOPED_SERVICE_BINDING_H_
#define BASE_FUCHSIA_SCOPED_SERVICE_BINDING_H_

#include <lib/fidl/cpp/binding.h>

#include "base/bind.h"
#include "base/fuchsia/services_directory.h"

namespace base {
namespace fuchsia {

class ServicesDirectory;

template <typename Interface>
class ScopedServiceBinding {
 public:
  // |services_directory| and |impl| must outlive the binding.
  ScopedServiceBinding(ServicesDirectory* services_directory, Interface* impl)
      : directory_(services_directory), binding_(impl) {
    directory_->AddService(
        Interface::Name_,
        BindRepeating(&ScopedServiceBinding::BindClient, Unretained(this)));
  }

  ~ScopedServiceBinding() { directory_->RemoveService(Interface::Name_); }

 private:
  void BindClient(ScopedZxHandle channel) {
    binding_.Bind(typename fidl::InterfaceRequest<Interface>(
        zx::channel(channel.release())));
  }

  ServicesDirectory* directory_;
  fidl::Binding<Interface> binding_;

  DISALLOW_COPY_AND_ASSIGN(ScopedServiceBinding);
};

}  // namespace fuchsia
}  // namespace base

#endif  // BASE_FUCHSIA_SCOPED_SERVICE_BINDING_H_