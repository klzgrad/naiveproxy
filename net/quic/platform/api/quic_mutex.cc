// Copyright (c) 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/platform/api/quic_mutex.h"

namespace net {

void QuicMutex::WriterLock() {
  impl_.WriterLock();
}

void QuicMutex::WriterUnlock() {
  impl_.WriterUnlock();
}

void QuicMutex::ReaderLock() {
  impl_.ReaderLock();
}

void QuicMutex::ReaderUnlock() {
  impl_.ReaderUnlock();
}

void QuicMutex::AssertReaderHeld() const {
  impl_.AssertReaderHeld();
}

QuicReaderMutexLock::QuicReaderMutexLock(QuicMutex* lock) : lock_(lock) {
  lock->ReaderLock();
}

QuicReaderMutexLock::~QuicReaderMutexLock() {
  lock_->ReaderUnlock();
}

QuicWriterMutexLock::QuicWriterMutexLock(QuicMutex* lock) : lock_(lock) {
  lock->WriterLock();
}

QuicWriterMutexLock::~QuicWriterMutexLock() {
  lock_->WriterUnlock();
}

}  // namespace net
