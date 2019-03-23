// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.support.annotation.VisibleForTesting;

import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;
import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

/**
 * Used to assert that clean-up logic has been run before an object is GC'ed.
 *
 * Class is a no-op withen DCHECK_IS_ON=false, and is entirely removed by
 * proguard (enforced via -checkdiscard).
 *
 * Usage:
 * class MyClassWithCleanup {
 *     private final mGcStateAssert = GcStateAssert.create(this);
 *
 *     public void destroy() {
 *         // If mGcStateAssert is GC'ed before this is called, it will throw an exception
 *         // with a stack trace showing the stack during GcStateAssert.create().
 *         GcStateAssert.setSafeToGc(mGcStateAssert, true);
 *     }
 * }
 */
public class GcStateAssert {
    interface TestHook {
        void onCleaned(WrappedReference ref, String msg);
    }

    // Used only for unit test.
    static TestHook sTestHook;

    @VisibleForTesting
    final WrappedReference mWrapper;

    @VisibleForTesting
    static class WrappedReference extends PhantomReference<Object> {
        boolean mSafeToGc;
        final Class<?> mTargetClass;
        final Throwable mCreationException;

        public WrappedReference(Object target, boolean safeToGc) {
            super(target, sReferenceQueue);
            mSafeToGc = safeToGc;
            mTargetClass = target.getClass();
            // Create an exception to capture stack trace of when object was created.
            mCreationException = new RuntimeException();
            sActiveWrappers.add(this);
        }

        private static ReferenceQueue<Object> sReferenceQueue = new ReferenceQueue<>();
        private static Set<WrappedReference> sActiveWrappers =
                Collections.synchronizedSet(new HashSet<>());

        static {
            new Thread("GcStateAssertQueue") {
                {
                    setDaemon(true);
                    start();
                }

                @Override
                public void run() {
                    while (true) {
                        try {
                            // This sleeps until a wrapper is available.
                            WrappedReference wrapper = (WrappedReference) sReferenceQueue.remove();
                            sActiveWrappers.remove(wrapper);
                            if (!wrapper.mSafeToGc) {
                                String msg = String.format(
                                        "Object of type %s was GC'ed without cleanup. Refer to "
                                                + "\"Caused by\" for where object was created.",
                                        wrapper.mTargetClass.getName());
                                if (sTestHook != null) {
                                    sTestHook.onCleaned(wrapper, msg);
                                } else {
                                    throw new RuntimeException(msg, wrapper.mCreationException);
                                }
                            } else if (sTestHook != null) {
                                sTestHook.onCleaned(wrapper, null);
                            }
                        } catch (InterruptedException e) {
                            throw new RuntimeException(e);
                        }
                    }
                }
            };
        }
    }

    private GcStateAssert(WrappedReference wrapper) {
        mWrapper = wrapper;
    }

    public static GcStateAssert create(Object target) {
        return create(target, false);
    }

    public static GcStateAssert create(Object target, boolean safeToGc) {
        if (!BuildConfig.DCHECK_IS_ON) {
            return null;
        }
        return new GcStateAssert(new WrappedReference(target, safeToGc));
    }

    public static void setSafeToGc(GcStateAssert asserter, boolean value) {
        if (BuildConfig.DCHECK_IS_ON) {
            // asserter is never null when DCHECK_IS_ON.
            asserter.mWrapper.mSafeToGc = value;
        }
    }
}
