// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/filtered_service_directory.h"

#include <lib/fdio/util.h>
#include <lib/zx/channel.h>

#include "base/bind.h"
#include "base/fuchsia/component_context.h"
#include "base/fuchsia/fuchsia_logging.h"

namespace base {
namespace fuchsia {

FilteredServiceDirectory::FilteredServiceDirectory(
    ComponentContext* component_context)
    : component_context_(component_context) {
  zx::channel server_channel;
  zx_status_t status =
      zx::channel::create(0, &server_channel, &directory_client_channel_);
  ZX_CHECK(status == ZX_OK, status) << "zx_channel_create()";

  service_directory_ =
      std::make_unique<ServiceDirectory>(std::move(server_channel));
}

FilteredServiceDirectory::~FilteredServiceDirectory() {
  service_directory_->RemoveAllServices();
}

void FilteredServiceDirectory::AddService(const char* service_name) {
  service_directory_->AddService(
      service_name,
      base::BindRepeating(&FilteredServiceDirectory::HandleRequest,
                          base::Unretained(this), service_name));
}

zx::channel FilteredServiceDirectory::ConnectClient() {
  zx::channel server_channel;
  zx::channel client_channel;
  zx_status_t status = zx::channel::create(0, &server_channel, &client_channel);
  ZX_CHECK(status == ZX_OK, status) << "zx_channel_create()";

  // ServiceDirectory puts public services under ./public . Connect to that
  // directory and return client handle for the connection,
  status = fdio_service_connect_at(directory_client_channel_.get(), "public",
                                   server_channel.release());
  ZX_CHECK(status == ZX_OK, status) << "fdio_service_connect_at()";

  return client_channel;
}

void FilteredServiceDirectory::HandleRequest(const char* service_name,
                                             zx::channel channel) {
  component_context_->ConnectToService(
      FidlInterfaceRequest::CreateFromChannelUnsafe(service_name,
                                                    std::move(channel)));
}

}  // namespace fuchsia
}  // namespace base
