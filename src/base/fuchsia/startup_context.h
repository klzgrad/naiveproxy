// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FUCHSIA_STARTUP_CONTEXT_H_
#define BASE_FUCHSIA_STARTUP_CONTEXT_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <memory>

#include "base/base_export.h"
#include "base/fuchsia/service_directory.h"
#include "base/fuchsia/service_directory_client.h"
#include "base/macros.h"

namespace base {
namespace fuchsia {

// Helper for unpacking a fuchsia.sys.StartupInfo and creating convenience
// wrappers for the various fields (e.g. the incoming & outgoing service
// directories, resolve launch URL etc).
// Embedders may derived from StartupContext to e.g. add bound pointers to
// embedder-specific services, as required.
class BASE_EXPORT StartupContext {
 public:
  explicit StartupContext(::fuchsia::sys::StartupInfo startup_info);
  virtual ~StartupContext();

  // Returns the namespace of services published for use by the component.
  const ServiceDirectoryClient* incoming_services() const {
    DCHECK(incoming_services_);
    return incoming_services_.get();
  }

  // Returns the outgoing directory into which this component binds services.
  // Note that all services should be bound immediately after the first call to
  // this API, before returning control to the message loop, at which point we
  // will start processing service connection requests.
  ServiceDirectory* public_services();

 private:
  ::fuchsia::sys::StartupInfo startup_info_;

  std::unique_ptr<ServiceDirectoryClient> incoming_services_;
  std::unique_ptr<ServiceDirectory> public_services_;

  // TODO(https://crbug.com/933834): Remove these when we migrate to the new
  // component manager APIs.
  ::fuchsia::sys::ServiceProviderPtr additional_services_;
  std::unique_ptr<ServiceDirectory> additional_services_directory_;

  DISALLOW_COPY_AND_ASSIGN(StartupContext);
};

}  // namespace fuchsia
}  // namespace base

#endif  // BASE_FUCHSIA_STARTUP_CONTEXT_H_
