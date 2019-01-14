// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DISK_CACHE_SIMPLE_SIMPLE_NET_LOG_PARAMETERS_H_
#define NET_DISK_CACHE_SIMPLE_SIMPLE_NET_LOG_PARAMETERS_H_

#include "net/log/net_log_parameters_callback.h"

// This file augments the functions in net/disk_cache/net_log_parameters.h to
// include ones that deal with specifics of the Simple Cache backend.
namespace disk_cache {

class SimpleEntryImpl;

// Creates a NetLog callback that returns parameters for the construction of a
// SimpleEntryImpl. Contains the entry's hash. |entry| can't be NULL and must
// outlive the returned callback.
net::NetLogParametersCallback CreateNetLogSimpleEntryConstructionCallback(
    const SimpleEntryImpl* entry);

// Creates a NetLog callback that returns parameters for the result of calling
// |CreateEntry| or |OpenEntry| on a SimpleEntryImpl. Contains the |net_error|
// and, if successful, the entry's key. |entry| can't be NULL and must outlive
// the returned callback.
net::NetLogParametersCallback CreateNetLogSimpleEntryCreationCallback(
    const SimpleEntryImpl* entry,
    int net_error);

}  // namespace disk_cache

#endif  // NET_DISK_CACHE_SIMPLE_SIMPLE_NET_LOG_PARAMETERS_H_
