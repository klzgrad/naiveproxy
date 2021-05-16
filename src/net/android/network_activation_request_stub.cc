// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/android/network_activation_request.h"

namespace net {
namespace android {

NetworkActivationRequest::NetworkActivationRequest(TransportType transport)
    : task_runner_(base::SequencedTaskRunnerHandle::Get()) {
  weak_self_ = weak_ptr_factory_.GetWeakPtr();
}

NetworkActivationRequest::~NetworkActivationRequest() {
}

void NetworkActivationRequest::NotifyAvailable(JNIEnv* env,
                                               NetworkHandle network) {
}

void NetworkActivationRequest::NotifyAvailableOnCorrectSequence(
    NetworkHandle network) {
}

}  // namespace android
}  // namespace net
