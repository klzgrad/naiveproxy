// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/socket/socket.h"

#include "net/base/net_errors.h"

namespace net {

const base::Feature Socket::kReadIfReadyExperiment{
    "SocketReadIfReady", base::FEATURE_ENABLED_BY_DEFAULT};

int Socket::ReadIfReady(IOBuffer* buf,
                        int buf_len,
                        const CompletionCallback& callback) {
  return ERR_READ_IF_READY_NOT_IMPLEMENTED;
}

}  // namespace net
