// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test.task;

/** Helpers that allow base::ThreadPoolInstance to be initialized or shutdown for testing. */
public class ThreadPoolTestHelpers {
    /**
     * Initializes base::ThreadPoolInstance with default params.
     */
    public static void enableThreadPoolExecutionForTesting() {
        nativeEnableThreadPoolExecutionForTesting();
    }

    /**
     * Shuts down base::ThreadPoolInstance.
     */
    public static void disableThreadPoolExecutionForTesting() {
        nativeDisableThreadPoolExecutionForTesting();
    }

    private static native void nativeEnableThreadPoolExecutionForTesting();
    private static native void nativeDisableThreadPoolExecutionForTesting();
}
