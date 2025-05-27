// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/observer_list_internal.h"

namespace base::internal {

CheckedObserverAdapter::CheckedObserverAdapter(const CheckedObserver* observer)
    : weak_ptr_(observer->factory_.GetWeakPtr()) {}

CheckedObserverAdapter::CheckedObserverAdapter(CheckedObserverAdapter&& other) =
    default;
CheckedObserverAdapter& CheckedObserverAdapter::operator=(
    CheckedObserverAdapter&& other) = default;
CheckedObserverAdapter::~CheckedObserverAdapter() = default;

}  // namespace base::internal
