// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.util;

import java.lang.ref.WeakReference;

/**
 * Util for doing garbage collection tests.
 */
public class GarbageCollectionTestUtil {
    /**
     * Do garbage collection and see if an object is released.
     * @param reference A {@link WeakReference} pointing to the object.
     * @return Whether the object can be garbage-collected.
     */
    public static boolean isGarbageCollected(WeakReference<?> reference) {
        Runtime runtime = Runtime.getRuntime();
        runtime.runFinalization();
        runtime.gc();
        return reference.get() == null;
    }
}
