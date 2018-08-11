// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/fuchsia/fidl_interface_request.h"

namespace base {
namespace fuchsia {

FidlInterfaceRequest::FidlInterfaceRequest(FidlInterfaceRequest&& moved) =
    default;

FidlInterfaceRequest::FidlInterfaceRequest(const char* interface_name,
                                           ScopedZxHandle channel)
    : interface_name_(interface_name), channel_(std::move(channel)) {}
FidlInterfaceRequest::~FidlInterfaceRequest() = default;

ScopedZxHandle FidlInterfaceRequest::TakeChannel() {
  DCHECK(channel_);
  return std::move(channel_);
}

}  // namespace fuchsia
}  // namespace base