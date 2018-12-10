// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FUCHSIA_FIDL_INTERFACE_REQUEST_H_
#define BASE_FUCHSIA_FIDL_INTERFACE_REQUEST_H_

#include <lib/zx/channel.h>

#include "base/base_export.h"
#include "base/macros.h"
#include "base/strings/string_piece.h"

namespace fidl {

template <typename Interface>
class InterfaceRequest;

template <typename Interface>
class InterfacePtr;

template <typename Interface>
class SynchronousInterfacePtr;

}  // namespace fidl

namespace base {
namespace fuchsia {

// A request for a FIDL interface. FidlInterfaceRequest contains interface name
// and channel handle. Interface consumers create FidlInterfaceRequest when they
// need to connect to a service. FidlInterfaceRequest is resolved when the
// channel is passed to the service implementation, e.g. through
// ComponentContext.
class BASE_EXPORT FidlInterfaceRequest {
 public:
  template <typename Interface>
  explicit FidlInterfaceRequest(fidl::InterfaceRequest<Interface> request)
      : FidlInterfaceRequest(Interface::Name_, request.TakeChannel()) {}

  // Creates a new request for |Interface| and binds the client end to the
  // |stub|. |stub| can be used immediately after the request is created, even
  // before the request is passed to the service that implements the interface.
  template <typename Interface>
  explicit FidlInterfaceRequest(fidl::InterfacePtr<Interface>* stub)
      : FidlInterfaceRequest(stub->NewRequest()) {}

  template <typename Interface>
  explicit FidlInterfaceRequest(fidl::SynchronousInterfacePtr<Interface>* stub)
      : FidlInterfaceRequest(stub->NewRequest()) {}

  FidlInterfaceRequest(FidlInterfaceRequest&&);
  ~FidlInterfaceRequest();

  // Creates an interface request from the specified |channel|. Caller must
  // ensure that the specified |interface_name| is valid for the specified
  // |channel|.
  static FidlInterfaceRequest CreateFromChannelUnsafe(
      const char* interface_name,
      zx::channel channel);

  bool is_valid() const { return interface_name_ && channel_; }

  const char* interface_name() const { return interface_name_; }

  // Extracts the channel handle to be passed to service implementation. The
  // request becomes invalid after this call, i.e. TakeChannel() can be called
  // only once.
  zx::channel TakeChannel();

 private:
  FidlInterfaceRequest(const char* interface_name, zx::channel channel);

  const char* interface_name_;
  zx::channel channel_;

  DISALLOW_COPY_AND_ASSIGN(FidlInterfaceRequest);
};

}  // namespace fuchsia
}  // namespace base

#endif  // BASE_FUCHSIA_FIDL_INTERFACE_REQUEST_H_
