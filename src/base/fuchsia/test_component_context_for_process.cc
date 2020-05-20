// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/test_component_context_for_process.h"

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/cpp/component_context.h>

#include "base/fuchsia/default_context.h"
#include "base/fuchsia/filtered_service_directory.h"
#include "base/fuchsia/fuchsia_logging.h"
#include "base/run_loop.h"

namespace base {

TestComponentContextForProcess::TestComponentContextForProcess() {
  // TODO(https://crbug.com/1038786): Migrate to sys::ComponentContextProvider
  // once it provides access to an sys::OutgoingDirectory or PseudoDir through
  // which to publish additional_services().

  // Set up |incoming_services_| to use the ServiceDirectory from the current
  // default ComponentContext to fetch services from.
  context_services_ = std::make_unique<fuchsia::FilteredServiceDirectory>(
      base::fuchsia::ComponentContextForCurrentProcess()->svc().get());

  // Create a ServiceDirectory backed by the contents of |incoming_directory|.
  fidl::InterfaceHandle<::fuchsia::io::Directory> incoming_directory;
  context_services_->ConnectClient(incoming_directory.NewRequest());
  auto incoming_services =
      std::make_shared<sys::ServiceDirectory>(std::move(incoming_directory));

  // Create the ComponentContext with the incoming directory connected to the
  // directory of |context_services_| published by the test, and with a request
  // for the process' root outgoing directory.
  fidl::InterfaceHandle<::fuchsia::io::Directory> published_root_directory;
  old_context_ = ReplaceComponentContextForCurrentProcessForTest(
      std::make_unique<sys::ComponentContext>(
          std::move(incoming_services),
          published_root_directory.NewRequest().TakeChannel()));

  // Connect to the "/svc" directory of the |published_root_directory| and wrap
  // that into a ServiceDirectory.
  fidl::InterfaceHandle<::fuchsia::io::Directory> published_services;
  zx_status_t status = fdio_service_connect_at(
      published_root_directory.channel().get(), "svc",
      published_services.NewRequest().TakeChannel().release());
  ZX_CHECK(status == ZX_OK, status) << "fdio_service_connect_at() to /svc";
  published_services_ =
      std::make_unique<sys::ServiceDirectory>(std::move(published_services));
}

TestComponentContextForProcess::~TestComponentContextForProcess() {
  ReplaceComponentContextForCurrentProcessForTest(std::move(old_context_));
}

sys::OutgoingDirectory* TestComponentContextForProcess::additional_services() {
  return context_services_->outgoing_directory();
}

void TestComponentContextForProcess::AddServices(
    base::span<const base::StringPiece> services) {
  for (auto service : services)
    context_services_->AddService(service);
}

}  // namespace base
