// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import org.junit.Assert;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.BlockJUnit4ClassRunner;

import org.chromium.base.test.util.Feature;

/**
 * Tests for NonThreadSafe.
 */
@RunWith(BlockJUnit4ClassRunner.class)
public class NonThreadSafeTest {
    /**
     * Test for creating and using on the same thread
     */
    @Test
    @Feature({"Android-AppBase"})
    public void testCreateAndUseOnSameThread() {
        NonThreadSafe t = new NonThreadSafe();
        Assert.assertTrue(t.calledOnValidThread());
    }

    /**
     * Test if calledOnValidThread returns false if used on another thread.
     */
    @Test
    @Feature({"Android-AppBase"})
    public void testCreateAndUseOnDifferentThread() {
        final NonThreadSafe t = new NonThreadSafe();

        new Thread(new Runnable() {
            @Override
            public void run() {
                Assert.assertFalse(t.calledOnValidThread());
            }
        }).start();
    }

    /**
     * Test if detachFromThread reassigns the thread.
     */
    @Test
    @Feature({"Android-AppBase"})
    public void testDetachFromThread() {
        final NonThreadSafe t = new NonThreadSafe();
        Assert.assertTrue(t.calledOnValidThread());
        t.detachFromThread();

        new Thread(new Runnable() {
            @Override
            public void run() {
                Assert.assertTrue(t.calledOnValidThread());
                Assert.assertTrue(t.calledOnValidThread());
            }
        }).start();
    }
}
