// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(b/178613777): Remove this file.

#ifndef QUICHE_QUIC_PLATFORM_API_QUIC_MUTEX_H_
#define QUICHE_QUIC_PLATFORM_API_QUIC_MUTEX_H_

#include "quiche/common/platform/api/quiche_mutex.h"

#define QUIC_EXCLUSIVE_LOCKS_REQUIRED QUICHE_EXCLUSIVE_LOCKS_REQUIRED
#define QUIC_GUARDED_BY QUICHE_GUARDED_BY
#define QUIC_LOCKABLE QUICHE_LOCKABLE
#define QUIC_LOCKS_EXCLUDED QUICHE_LOCKS_EXCLUDED
#define QUIC_SHARED_LOCKS_REQUIRED QUICHE_SHARED_LOCKS_REQUIRED
#define QUIC_EXCLUSIVE_LOCK_FUNCTION QUICHE_EXCLUSIVE_LOCK_FUNCTION
#define QUIC_UNLOCK_FUNCTION QUICHE_UNLOCK_FUNCTION
#define QUIC_SHARED_LOCK_FUNCTION QUICHE_SHARED_LOCK_FUNCTION
#define QUIC_SCOPED_LOCKABLE QUICHE_SCOPED_LOCKABLE
#define QUIC_ASSERT_SHARED_LOCK QUICHE_ASSERT_SHARED_LOCK

namespace quic {

using QuicMutex = quiche::QuicheMutex;
using QuicReaderMutexLock = quiche::QuicheReaderMutexLock;
using QuicWriterMutexLock = quiche::QuicheWriterMutexLock;
using QuicNotification = quiche::QuicheNotification;

}  // namespace quic

#endif  // QUICHE_QUIC_PLATFORM_API_QUIC_MUTEX_H_
