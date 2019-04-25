// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/service_directory_client.h"

#include <lib/fdio/util.h>
#include <utility>

#include "base/fuchsia/fuchsia_logging.h"
#include "base/logging.h"
#include "base/no_destructor.h"

namespace base {
namespace fuchsia {

namespace {

// static
fidl::InterfaceHandle<::fuchsia::io::Directory> ConnectToServiceRoot() {
  fidl::InterfaceHandle<::fuchsia::io::Directory> directory;
  zx_status_t result = fdio_service_connect(
      "/svc/.", directory.NewRequest().TakeChannel().release());
  ZX_CHECK(result == ZX_OK, result) << "Failed to open /svc";
  return directory;
}

// Singleton container for the process-global ServiceDirectoryClient instance.
std::unique_ptr<ServiceDirectoryClient>* ProcessServiceDirectoryClient() {
  static base::NoDestructor<std::unique_ptr<ServiceDirectoryClient>>
      service_directory_client_ptr(
          std::make_unique<ServiceDirectoryClient>(ConnectToServiceRoot()));
  return service_directory_client_ptr.get();
}

}  // namespace

ServiceDirectoryClient::ServiceDirectoryClient(
    fidl::InterfaceHandle<::fuchsia::io::Directory> directory)
    : directory_(std::move(directory)) {
  DCHECK(directory_);
}

ServiceDirectoryClient::~ServiceDirectoryClient() = default;

// static
const ServiceDirectoryClient* ServiceDirectoryClient::ForCurrentProcess() {
  return ProcessServiceDirectoryClient()->get();
}

zx_status_t ServiceDirectoryClient::ConnectToServiceUnsafe(
    const char* name,
    zx::channel request) const {
  DCHECK(request.is_valid());
  return fdio_service_connect_at(directory_.channel().get(), name,
                                 request.release());
}

ScopedServiceDirectoryClientForCurrentProcessForTest::
    ScopedServiceDirectoryClientForCurrentProcessForTest(
        fidl::InterfaceHandle<::fuchsia::io::Directory> directory)
    : old_client_(std::move(*ProcessServiceDirectoryClient())) {
  *ProcessServiceDirectoryClient() =
      std::make_unique<ServiceDirectoryClient>(std::move(directory));
  client_ = ProcessServiceDirectoryClient()->get();
}

ScopedServiceDirectoryClientForCurrentProcessForTest::
    ~ScopedServiceDirectoryClientForCurrentProcessForTest() {
  DCHECK_EQ(ProcessServiceDirectoryClient()->get(), client_);
  *ProcessServiceDirectoryClient() = std::move(old_client_);
}

}  // namespace fuchsia
}  // namespace base
