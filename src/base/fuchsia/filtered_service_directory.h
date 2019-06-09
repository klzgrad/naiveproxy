// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_FUCHSIA_FILTERED_SERVICE_DIRECTORY_H_
#define BASE_FUCHSIA_FILTERED_SERVICE_DIRECTORY_H_

#include "base/fuchsia/service_directory.h"

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/zx/channel.h>
#include <memory>

#include "base/macros.h"

namespace base {
namespace fuchsia {

class ServiceDirectoryClient;

// ServiceDirectory that uses the supplied ServiceDirectoryClient to satisfy
// requests for only a restricted set of services.
class BASE_EXPORT FilteredServiceDirectory {
 public:
  // Creates proxy that proxies requests to the specified service |directory|,
  // which must outlive the proxy.
  explicit FilteredServiceDirectory(const ServiceDirectoryClient* directory);
  ~FilteredServiceDirectory();

  // Adds the specified service to the list of whitelisted services.
  void AddService(const char* service_name);

  // Returns a client channel connected to the directory. The returned channel
  // can be passed to a sandboxed process to be used for /svc namespace.
  fidl::InterfaceHandle<::fuchsia::io::Directory> ConnectClient();

 private:
  void HandleRequest(const char* service_name, zx::channel channel);

  const ServiceDirectoryClient* const directory_;
  std::unique_ptr<ServiceDirectory> outgoing_directory_;

  // Client side of the channel used by |outgoing_directory_|.
  fidl::InterfaceHandle<::fuchsia::io::Directory> outgoing_directory_client_;

  DISALLOW_COPY_AND_ASSIGN(FilteredServiceDirectory);
};

}  // namespace fuchsia
}  // namespace base

#endif  // BASE_FUCHSIA_FILTERED_SERVICE_DIRECTORY_H_
