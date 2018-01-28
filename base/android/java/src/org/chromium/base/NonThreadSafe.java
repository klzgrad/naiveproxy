// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import org.chromium.base.annotations.SuppressFBWarnings;

/**
 * NonThreadSafe is a helper class used to help verify that methods of a
 * class are called from the same thread.
 */
public class NonThreadSafe {
    private Long mThreadId;

    public NonThreadSafe() {
        ensureThreadIdAssigned();
    }

    /**
     * Changes the thread that is checked for in CalledOnValidThread. This may
     * be useful when an object may be created on one thread and then used
     * exclusively on another thread.
     */
    @SuppressFBWarnings("CHROMIUM_SYNCHRONIZED_METHOD")
    @VisibleForTesting
    public synchronized void detachFromThread() {
        mThreadId = null;
    }

    /**
     * Checks if the method is called on the valid thread.
     * Assigns the current thread if no thread was assigned.
     */
    @SuppressFBWarnings("CHROMIUM_SYNCHRONIZED_METHOD")
    public synchronized boolean calledOnValidThread() {
        ensureThreadIdAssigned();
        return mThreadId.equals(Thread.currentThread().getId());
    }

    private void ensureThreadIdAssigned() {
        if (mThreadId == null) mThreadId = Thread.currentThread().getId();
    }
}
