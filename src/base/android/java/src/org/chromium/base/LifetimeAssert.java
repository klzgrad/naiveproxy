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
 *     private final mLifetimeAssert = LifetimeAssert.create(this);
 *
 *     public void destroy() {
 *         // If mLifetimeAssert is GC'ed before this is called, it will throw an exception
 *         // with a stack trace showing the stack during LifetimeAssert.create().
 *         LifetimeAssert.setSafeToGc(mLifetimeAssert, true);
 *     }
 * }
 */
public class LifetimeAssert {
    interface TestHook {
        void onCleaned(WrappedReference ref, String msg);
    }

    /**
     * Thrown for failed assertions.
     */
    static class LifetimeAssertException extends RuntimeException {
        LifetimeAssertException(String msg, Throwable causedBy) {
            super(msg, causedBy);
        }
    }

    /**
     * For capturing where objects were created.
     */
    private static class CreationException extends RuntimeException {
        CreationException() {
            super("vvv This is where object was created. vvv");
        }
    }

    // Used only for unit test.
    static TestHook sTestHook;

    @VisibleForTesting
    final WrappedReference mWrapper;

    @VisibleForTesting
    static class WrappedReference extends PhantomReference<Object> {
        boolean mSafeToGc;
        final Class<?> mTargetClass;
        final CreationException mCreationException;

        public WrappedReference(
                Object target, CreationException creationException, boolean safeToGc) {
            super(target, sReferenceQueue);
            mCreationException = creationException;
            mSafeToGc = safeToGc;
            mTargetClass = target.getClass();
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
                                    throw new LifetimeAssertException(
                                            msg, wrapper.mCreationException);
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

    private LifetimeAssert(WrappedReference wrapper) {
        mWrapper = wrapper;
    }

    public static LifetimeAssert create(Object target) {
        if (!BuildConfig.DCHECK_IS_ON) {
            return null;
        }
        return new LifetimeAssert(new WrappedReference(target, new CreationException(), false));
    }

    public static LifetimeAssert create(Object target, boolean safeToGc) {
        if (!BuildConfig.DCHECK_IS_ON) {
            return null;
        }
        return new LifetimeAssert(new WrappedReference(target, new CreationException(), safeToGc));
    }

    public static void setSafeToGc(LifetimeAssert asserter, boolean value) {
        if (BuildConfig.DCHECK_IS_ON) {
            // asserter is never null when DCHECK_IS_ON.
            asserter.mWrapper.mSafeToGc = value;
        }
    }

    public static void assertAllInstancesDestroyedForTesting() throws LifetimeAssertException {
        if (!BuildConfig.DCHECK_IS_ON) {
            return;
        }
        synchronized (WrappedReference.sActiveWrappers) {
            for (WrappedReference ref : WrappedReference.sActiveWrappers) {
                if (!ref.mSafeToGc) {
                    String msg = String.format(
                            "Object of type %s was not destroyed after test completed. Refer to "
                                    + "\"Caused by\" for where object was created.",
                            ref.mTargetClass.getName());
                    throw new LifetimeAssertException(msg, ref.mCreationException);
                }
            }
        }
    }
}
