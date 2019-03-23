// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;

import org.chromium.base.test.BaseRobolectricTestRunner;

/**
 * junit tests for {@link GcStateAssert}.
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class GcStateAssertTest {
    private static class TestClass {
        // Put assert inside of a test class to mirror typical api usage.
        final GcStateAssert mGcStateAssert = GcStateAssert.create(this);
    }

    private final Object mLock = new Object();
    private TestClass mTestClass;
    private GcStateAssert.WrappedReference mTargetRef;
    private boolean mFound;
    private String mHookMessage;

    @Before
    public void setUp() {
        if (!BuildConfig.DCHECK_IS_ON) {
            return;
        }
        mTestClass = new TestClass();
        mTargetRef = mTestClass.mGcStateAssert.mWrapper;
        mFound = false;
        mHookMessage = null;
        GcStateAssert.sTestHook = (ref, msg) -> {
            if (ref == mTargetRef) {
                synchronized (mLock) {
                    mFound = true;
                    mHookMessage = msg;
                    mLock.notify();
                }
            }
        };
    }

    @After
    public void tearDown() {
        if (!BuildConfig.DCHECK_IS_ON) {
            return;
        }
        GcStateAssert.sTestHook = null;
    }

    private void runTest(boolean setSafe) {
        if (!BuildConfig.DCHECK_IS_ON) {
            return;
        }

        synchronized (mLock) {
            if (setSafe) {
                GcStateAssert.setSafeToGc(mTestClass.mGcStateAssert, true);
            }
            // Null out field to make reference GC'able.
            mTestClass = null;
            // Call System.gc() until the background thread notices the reference.
            for (int i = 0; i < 10 && !mFound; ++i) {
                System.gc();
                try {
                    mLock.wait(200);
                } catch (InterruptedException e) {
                }
            }
            Assert.assertTrue(mFound);
            if (setSafe) {
                Assert.assertNull(mHookMessage);
            } else {
                Assert.assertNotNull(mHookMessage);
            }
        }
    }

    @Test
    public void testSafeGc() {
        runTest(true);
    }

    @Test
    public void testUnsafeGc() {
        runTest(false);
    }
}
