// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/filtered_service_directory.h"

#include <fidl/fuchsia.io/cpp/common_types.h>
#include <lib/async/default.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/vfs/cpp/service.h>

#include <string_view>
#include <utility>

#include "base/fuchsia/fuchsia_logging.h"
#include "base/functional/bind.h"

namespace base {

FilteredServiceDirectory::FilteredServiceDirectory(
    std::shared_ptr<sys::ServiceDirectory> directory)
    : directory_(directory) {}

FilteredServiceDirectory::~FilteredServiceDirectory() = default;

zx_status_t FilteredServiceDirectory::AddService(
    std::string_view service_name) {
  return outgoing_directory_.AddPublicService(
      std::make_unique<vfs::Service>(
          [this, service_name = std::string(service_name)](
              zx::channel channel, async_dispatcher_t* dispatcher) {
            DCHECK_EQ(dispatcher, async_get_default_dispatcher());
            directory_->Connect(service_name, std::move(channel));
          }),
      std::string(service_name));
}

zx_status_t FilteredServiceDirectory::ConnectClient(
    fidl::InterfaceRequest<fuchsia::io::Directory> dir_request) {
  // sys::OutgoingDirectory puts public services under /svc. Open that
  // directory and return client handle for the connection.
  return outgoing_directory_.GetOrCreateDirectory("svc")->Serve(
      fuchsia_io::wire::kPermReadable,
      fidl::ServerEnd<fuchsia_io::Directory>(dir_request.TakeChannel()));
}

}  // namespace base
