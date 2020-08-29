// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.annotation.Config;

import org.chromium.base.test.BaseRobolectricTestRunner;

/**
 * Test class for {@link CallbackController}, which also describes typical usage.
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class CallbackControllerTest {
    private boolean mExecutionCompleted;

    @Test
    public void testInstanceCallback() {
        CallbackController mCallbackController = new CallbackController();
        Callback<Boolean> wrapped = mCallbackController.makeCancelable(this::setExecutionCompleted);
        mExecutionCompleted = false;
        wrapped.onResult(true);
        assertTrue(mExecutionCompleted);

        // Execution possible multiple times.
        mExecutionCompleted = false;
        wrapped.onResult(true);
        assertTrue(mExecutionCompleted);

        // Won't trigger after CallbackController is destroyed.
        mExecutionCompleted = false;
        mCallbackController.destroy();
        wrapped.onResult(true);
        assertFalse(mExecutionCompleted);
    }

    @Test
    public void testlInstanceRunnable() {
        CallbackController mCallbackController = new CallbackController();
        Runnable wrapped = mCallbackController.makeCancelable(this::completeExection);
        mExecutionCompleted = false;
        wrapped.run();
        assertTrue(mExecutionCompleted);

        // Execution possible multiple times.
        mExecutionCompleted = false;
        wrapped.run();
        assertTrue(mExecutionCompleted);

        // Won't trigger after CallbackController is destroyed.
        mExecutionCompleted = false;
        mCallbackController.destroy();
        wrapped.run();
        assertFalse(mExecutionCompleted);
    }

    @Test
    public void testLambdaCallback() {
        CallbackController mCallbackController = new CallbackController();
        Callback<Boolean> wrapped =
                mCallbackController.makeCancelable(value -> setExecutionCompleted(value));
        mExecutionCompleted = false;
        wrapped.onResult(true);
        assertTrue(mExecutionCompleted);

        // Execution possible multiple times.
        mExecutionCompleted = false;
        wrapped.onResult(true);
        assertTrue(mExecutionCompleted);

        // Won't trigger after CallbackController is destroyed.
        mExecutionCompleted = false;
        mCallbackController.destroy();
        wrapped.onResult(true);
        assertFalse(mExecutionCompleted);
    }

    @Test
    public void testLambdaRunnable() {
        Runnable runnable = () -> setExecutionCompleted(true);
        CallbackController mCallbackController = new CallbackController();
        Runnable wrapped = mCallbackController.makeCancelable(() -> completeExection());
        mExecutionCompleted = false;
        wrapped.run();
        assertTrue(mExecutionCompleted);

        // Execution possible multiple times.
        mExecutionCompleted = false;
        wrapped.run();
        assertTrue(mExecutionCompleted);

        // Won't trigger after CallbackController is destroyed.
        mExecutionCompleted = false;
        mCallbackController.destroy();
        wrapped.run();
        assertFalse(mExecutionCompleted);
    }

    private void completeExection() {
        setExecutionCompleted(true);
    }

    private void setExecutionCompleted(boolean completed) {
        mExecutionCompleted = completed;
    }
}
