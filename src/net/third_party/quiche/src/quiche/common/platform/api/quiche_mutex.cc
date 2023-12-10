// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quiche/common/platform/api/quiche_mutex.h"

namespace quiche {

void QuicheMutex::WriterLock() { impl_.WriterLock(); }

void QuicheMutex::WriterUnlock() { impl_.WriterUnlock(); }

void QuicheMutex::ReaderLock() { impl_.ReaderLock(); }

void QuicheMutex::ReaderUnlock() { impl_.ReaderUnlock(); }

void QuicheMutex::AssertReaderHeld() const { impl_.AssertReaderHeld(); }

QuicheReaderMutexLock::QuicheReaderMutexLock(QuicheMutex* lock) : lock_(lock) {
  lock->ReaderLock();
}

QuicheReaderMutexLock::~QuicheReaderMutexLock() { lock_->ReaderUnlock(); }

QuicheWriterMutexLock::QuicheWriterMutexLock(QuicheMutex* lock) : lock_(lock) {
  lock->WriterLock();
}

QuicheWriterMutexLock::~QuicheWriterMutexLock() { lock_->WriterUnlock(); }

}  // namespace quiche
