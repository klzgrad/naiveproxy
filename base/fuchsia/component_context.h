// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FUCHSIA_COMPONENT_CONTEXT_H_
#define BASE_FUCHSIA_COMPONENT_CONTEXT_H_

#include <lib/zx/channel.h>

#include "base/base_export.h"
#include "base/fuchsia/fidl_interface_request.h"
#include "base/macros.h"
#include "base/strings/string_piece.h"

namespace fidl {

template <typename Interface>
class InterfacePtr;

template <typename Interface>
class SynchronousInterfacePtr;

}  // namespace fidl

namespace base {
namespace fuchsia {

// Provides access to the component's environment.
class BASE_EXPORT ComponentContext {
 public:
  explicit ComponentContext(zx::channel service_root);
  ~ComponentContext();

  // Returns default ComponentContext instance for the current process. It uses
  // /srv namespace to connect to environment services.
  static ComponentContext* GetDefault();

  // Satisfies the interface |request| by binding the channel to a service.
  zx_status_t ConnectToService(FidlInterfaceRequest request);

  // Same as above, but returns interface pointer instead of taking a request.
  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToService() {
    fidl::InterfacePtr<Interface> result;
    ConnectToService(FidlInterfaceRequest(&result));
    return result;
  }

  // Connects to an environment service and returns synchronous interface
  // implementation.
  template <typename Interface>
  fidl::SynchronousInterfacePtr<Interface> ConnectToServiceSync() {
    fidl::SynchronousInterfacePtr<Interface> result;
    ConnectToService(FidlInterfaceRequest(&result));
    return result;
  }

 private:
  zx::channel service_root_;

  DISALLOW_COPY_AND_ASSIGN(ComponentContext);
};

}  // namespace fuchsia
}  // namespace base

#endif  // BASE_FUCHSIA_COMPONENT_CONTEXT_H_
