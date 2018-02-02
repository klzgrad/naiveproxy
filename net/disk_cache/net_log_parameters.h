// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_DISK_CACHE_NET_LOG_PARAMETERS_H_
#define NET_DISK_CACHE_NET_LOG_PARAMETERS_H_

#include <stdint.h>

#include <string>

#include "net/log/net_log_parameters_callback.h"

namespace net {
struct NetLogSource;
}

// This file contains a set of functions to create NetLogParametersCallbacks
// shared by EntryImpls and MemEntryImpls.
namespace disk_cache {

class Entry;

// Creates a NetLog callback that returns parameters for the creation of an
// Entry.  Contains the Entry's key and whether it was created or opened.
// |entry| can't be NULL, must support GetKey(), and must outlive the returned
// callback.
net::NetLogParametersCallback CreateNetLogEntryCreationCallback(
    const Entry* entry,
    bool created);

// Creates a NetLog callback that returns parameters for start of a non-sparse
// read or write of an Entry.  For reads, |truncate| must be false.
net::NetLogParametersCallback CreateNetLogReadWriteDataCallback(int index,
                                                                int offset,
                                                                int buf_len,
                                                                bool truncate);

// Creates a NetLog callback that returns parameters for when a non-sparse
// read or write completes.  For reads, |truncate| must be false.
// |bytes_copied| is either the number of bytes copied or a network error
// code.  |bytes_copied| must not be ERR_IO_PENDING, as it's not a valid
// result for an operation.
net::NetLogParametersCallback CreateNetLogReadWriteCompleteCallback(
    int bytes_copied);

// Creates a NetLog callback that returns parameters for when a sparse
// operation is started.
net::NetLogParametersCallback CreateNetLogSparseOperationCallback(
    int64_t offset,
    int buf_len);

// Creates a NetLog callback that returns parameters for when a read or write
// for a sparse entry's child is started.
net::NetLogParametersCallback CreateNetLogSparseReadWriteCallback(
    const net::NetLogSource& source,
    int child_len);

// Creates a NetLog callback that returns parameters for when a call to
// GetAvailableRange returns.
net::NetLogParametersCallback CreateNetLogGetAvailableRangeResultCallback(
    int64_t start,
    int result);

}  // namespace disk_cache

#endif  // NET_DISK_CACHE_NET_LOG_PARAMETERS_H_
