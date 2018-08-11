// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/services_directory.h"

#include <lib/async/default.h>
#include <lib/svc/dir.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "base/fuchsia/fuchsia_logging.h"
#include "base/message_loop/message_loop_current.h"
#include "base/no_destructor.h"

namespace base {
namespace fuchsia {

ServicesDirectory::ServicesDirectory(ScopedZxHandle directory_request) {
  zx_status_t status = svc_dir_create(async_get_default(),
                                      directory_request.release(), &svc_dir_);
  ZX_CHECK(status == ZX_OK, status);
}

ServicesDirectory::~ServicesDirectory() {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(services_.empty());

  zx_status_t status = svc_dir_destroy(svc_dir_);
  ZX_DCHECK(status == ZX_OK, status);
}

// static
ServicesDirectory* ServicesDirectory::GetDefault() {
  static base::NoDestructor<ServicesDirectory> directory(
      ScopedZxHandle(zx_get_startup_handle(PA_DIRECTORY_REQUEST)));
  return directory.get();
}

void ServicesDirectory::AddService(StringPiece name,
                                   ConnectServiceCallback connect_callback) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  DCHECK(services_.find(name) == services_.end());

  std::string name_str = name.as_string();
  services_[name_str] = connect_callback;
  zx_status_t status =
      svc_dir_add_service(svc_dir_, "public", name_str.c_str(), this,
                          &ServicesDirectory::HandleConnectRequest);
  ZX_DCHECK(status == ZX_OK, status);
}

void ServicesDirectory::RemoveService(StringPiece name) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);

  std::string name_str = name.as_string();

  auto it = services_.find(name_str);
  DCHECK(it != services_.end());
  services_.erase(it);

  zx_status_t status =
      svc_dir_remove_service(svc_dir_, "public", name_str.c_str());
  ZX_DCHECK(status == ZX_OK, status);
}

// static
void ServicesDirectory::HandleConnectRequest(void* context,
                                             const char* service_name,
                                             zx_handle_t service_request) {
  auto* directory = reinterpret_cast<ServicesDirectory*>(context);
  DCHECK_CALLED_ON_VALID_THREAD(directory->thread_checker_);

  auto it = directory->services_.find(service_name);

  // HandleConnectRequest() is expected to be called only for registered
  // services.
  DCHECK(it != directory->services_.end());

  it->second.Run(ScopedZxHandle(service_request));
}

}  // namespace fuchsia
}  // namespace base
