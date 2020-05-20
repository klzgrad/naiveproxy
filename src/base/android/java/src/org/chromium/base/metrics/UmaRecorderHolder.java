// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.metrics;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import javax.annotation.concurrent.GuardedBy;

/** Holds the {@link CachingUmaRecorder} used by {@link RecordHistogram}. */
public class UmaRecorderHolder {
    /** The instance held by this class. */
    private static CachingUmaRecorder sRecorder = new CachingUmaRecorder();

    /**
     * {@code null}, unless recording is currently disabled for testing. Exposed for use in peer
     * classes {e.g. AnimationFrameTimeHistogram}.
     * <p>
     * Use {@link #setDisabledForTests(boolean)} to set this value.
     * <p>
     * TODO(bttk@chromium.org): Fix dependency in AnimationFrameTimeHistogram, make this field
     *     private and rename to sDisabledForTestBy
     */
    @VisibleForTesting
    @Nullable
    public static Throwable sDisabledBy;

    /** Lock for {@code sDisabledDelegateForTest}. */
    private static final Object sLock = new Object();

    /** The delegate disabled by {@link #setDisabledForTests(boolean)}. */
    @GuardedBy("sLock")
    @Nullable
    private static UmaRecorder sDisabledDelegateForTest;

    /** Returns the {@link CachingUmaRecorder}. */
    /* package */ static CachingUmaRecorder get() {
        return sRecorder;
    }

    /** Starts forwarding metrics to the native code. Returns after the cache has been flushed. */
    public static void onLibraryLoaded() {
        synchronized (sLock) {
            if (sDisabledBy == null) {
                sRecorder.setDelegate(new NativeUmaRecorder());
            } else {
                // If metrics are disabled for test, use native when metrics get restored.
                sDisabledDelegateForTest = new NativeUmaRecorder();
            }
        }
    }

    /**
     * Tests may need to disable metrics. The value should be reset after the test done, to avoid
     * carrying over state to unrelated tests. <p> In JUnit tests this can be done automatically
     * using {@link org.chromium.base.metrics.test.DisableHistogramsRule}.
     */
    @VisibleForTesting
    public static void setDisabledForTests(boolean disabled) {
        synchronized (sLock) {
            if (disabled) {
                if (sDisabledBy != null) {
                    throw new IllegalStateException(
                            "Histograms are already disabled.", sDisabledBy);
                }
                sDisabledBy = new Throwable();
                sDisabledDelegateForTest = sRecorder.setDelegate(new NoopUmaRecorder());
            } else {
                sDisabledBy = null;
                sRecorder.setDelegate(sDisabledDelegateForTest);
                sDisabledDelegateForTest = null;
            }
        }
    }
}
