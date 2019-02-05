// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/component_context.h"

#include <lib/fdio/util.h>
#include <lib/zx/channel.h>
#include <utility>

#include "base/fuchsia/fuchsia_logging.h"
#include "base/logging.h"
#include "base/no_destructor.h"

namespace base {
namespace fuchsia {

namespace {

// static
zx::channel ConnectToServiceRoot() {
  zx::channel client_channel;
  zx::channel server_channel;
  zx_status_t result = zx::channel::create(0, &client_channel, &server_channel);
  ZX_CHECK(result == ZX_OK, result) << "zx_channel_create()";
  result = fdio_service_connect("/svc/.", server_channel.release());
  ZX_CHECK(result == ZX_OK, result) << "Failed to open /svc";
  return client_channel;
}

std::unique_ptr<ComponentContext>* DefaultComponentContext() {
  static base::NoDestructor<std::unique_ptr<ComponentContext>>
      component_context_ptr(
          std::make_unique<ComponentContext>(ConnectToServiceRoot()));
  return component_context_ptr.get();
}

}  // namespace

ComponentContext::ComponentContext(zx::channel service_root)
    : service_root_(std::move(service_root)) {
  DCHECK(service_root_);
}

ComponentContext::~ComponentContext() = default;

// static
ComponentContext* ComponentContext::GetDefault() {
  return DefaultComponentContext()->get();
}

zx_status_t ComponentContext::ConnectToService(FidlInterfaceRequest request) {
  DCHECK(request.is_valid());
  return fdio_service_connect_at(service_root_.get(), request.interface_name(),
                                 request.TakeChannel().release());
}

ScopedDefaultComponentContext::ScopedDefaultComponentContext(
    zx::channel service_root)
    : old_context_(std::move(*DefaultComponentContext())) {
  *DefaultComponentContext() =
      std::make_unique<ComponentContext>(std::move(service_root));
  context_ = DefaultComponentContext()->get();
}

ScopedDefaultComponentContext::~ScopedDefaultComponentContext() {
  DCHECK_EQ(DefaultComponentContext()->get(), context_);
  *DefaultComponentContext() = std::move(old_context_);
}

}  // namespace fuchsia
}  // namespace base
