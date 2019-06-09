// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/filtered_service_directory.h"

#include <lib/fdio/directory.h>
#include <utility>

#include "base/bind.h"
#include "base/fuchsia/fuchsia_logging.h"
#include "base/fuchsia/service_directory_client.h"

namespace base {
namespace fuchsia {

FilteredServiceDirectory::FilteredServiceDirectory(
    const ServiceDirectoryClient* directory)
    : directory_(directory) {
  outgoing_directory_ = std::make_unique<ServiceDirectory>(
      outgoing_directory_client_.NewRequest());
}

FilteredServiceDirectory::~FilteredServiceDirectory() {
  outgoing_directory_->RemoveAllServices();
}

void FilteredServiceDirectory::AddService(const char* service_name) {
  outgoing_directory_->AddServiceUnsafe(
      service_name,
      base::BindRepeating(&FilteredServiceDirectory::HandleRequest,
                          base::Unretained(this), service_name));
}

fidl::InterfaceHandle<::fuchsia::io::Directory>
FilteredServiceDirectory::ConnectClient() {
  fidl::InterfaceHandle<::fuchsia::io::Directory> client;

  // ServiceDirectory puts public services under ./svc . Connect to that
  // directory and return client handle for the connection,
  zx_status_t status =
      fdio_service_connect_at(outgoing_directory_client_.channel().get(), "svc",
                              client.NewRequest().TakeChannel().release());
  ZX_CHECK(status == ZX_OK, status) << "fdio_service_connect_at()";

  return client;
}

void FilteredServiceDirectory::HandleRequest(const char* service_name,
                                             zx::channel channel) {
  directory_->ConnectToServiceUnsafe(service_name, std::move(channel));
}

}  // namespace fuchsia
}  // namespace base
