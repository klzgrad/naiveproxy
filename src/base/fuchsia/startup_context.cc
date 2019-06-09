// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/startup_context.h"

#include <fuchsia/io/cpp/fidl.h>

#include "base/fuchsia/file_utils.h"

namespace base {
namespace fuchsia {

StartupContext::StartupContext(::fuchsia::sys::StartupInfo startup_info)
    : startup_info_(std::move(startup_info)) {
  // Component manager generates |flat_namespace|, so things are horribly broken
  // if |flat_namespace| is malformed.
  CHECK_EQ(startup_info_.flat_namespace.directories.size(),
           startup_info_.flat_namespace.paths.size());

  // Find the /svc directory and wrap it into a ServiceDirectoryClient.
  for (size_t i = 0; i < startup_info_.flat_namespace.paths.size(); ++i) {
    if (startup_info_.flat_namespace.paths[i] == kServiceDirectoryPath) {
      incoming_services_ = std::make_unique<ServiceDirectoryClient>(
          fidl::InterfaceHandle<::fuchsia::io::Directory>(
              std::move(startup_info_.flat_namespace.directories[i])));
      break;
    }
  }

  // TODO(https://crbug.com/933834): Remove these workarounds when we migrate to
  // the new component manager.
  if (!incoming_services_ && startup_info_.launch_info.flat_namespace) {
    LOG(WARNING) << "Falling back to LaunchInfo namespace";
    for (size_t i = 0;
         i < startup_info_.launch_info.flat_namespace->paths.size(); ++i) {
      if (startup_info_.launch_info.flat_namespace->paths[i] ==
          kServiceDirectoryPath) {
        incoming_services_ = std::make_unique<ServiceDirectoryClient>(
            fidl::InterfaceHandle<::fuchsia::io::Directory>(std::move(
                startup_info_.launch_info.flat_namespace->directories[i])));
        break;
      }
    }
  }
  if (!incoming_services_ && startup_info_.launch_info.additional_services) {
    LOG(WARNING) << "Falling back to additional ServiceList services";

    // Construct a ServiceDirectory and publish the additional services into it.
    fidl::InterfaceHandle<::fuchsia::io::Directory> incoming_directory;
    additional_services_.Bind(
        std::move(startup_info_.launch_info.additional_services->provider));
    additional_services_directory_ =
        std::make_unique<ServiceDirectory>(incoming_directory.NewRequest());
    for (auto& name : startup_info_.launch_info.additional_services->names) {
      additional_services_directory_->AddServiceUnsafe(
          name, base::BindRepeating(
                    &::fuchsia::sys::ServiceProvider::ConnectToService,
                    base::Unretained(additional_services_.get()), name));
    }

    // Publish those services to the caller as |incoming_services_|.
    incoming_services_ = std::make_unique<ServiceDirectoryClient>(
        fidl::InterfaceHandle<::fuchsia::io::Directory>(
            std::move(incoming_directory)));
  }
}

StartupContext::~StartupContext() = default;

ServiceDirectory* StartupContext::public_services() {
  if (!public_services_ && startup_info_.launch_info.directory_request) {
    public_services_ = std::make_unique<ServiceDirectory>(
        fidl::InterfaceRequest<::fuchsia::io::Directory>(
            std::move(startup_info_.launch_info.directory_request)));
  }
  return public_services_.get();
}

}  // namespace fuchsia
}  // namespace base
