// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test;

import org.chromium.base.annotations.JNINamespace;

/**
 * Class containing only static methods for querying the status of the reached code profiler.
 */
@JNINamespace("base::android")
public class ReachedCodeProfiler {
    private ReachedCodeProfiler() {}

    /**
     * @return Whether the reached code profiler is enabled.
     */
    public static boolean isEnabled() {
        return nativeIsReachedCodeProfilerEnabled();
    }

    /**
     * @return Whether the currently used version of native library supports the reached code
     *         profiler.
     */
    public static boolean isSupported() {
        return nativeIsReachedCodeProfilerSupported();
    }

    private static native boolean nativeIsReachedCodeProfilerEnabled();
    private static native boolean nativeIsReachedCodeProfilerSupported();
}
