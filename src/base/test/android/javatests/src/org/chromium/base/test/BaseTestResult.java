// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test;

import android.app.Instrumentation;
import android.content.Context;
import android.os.Bundle;
import android.os.SystemClock;

import junit.framework.TestCase;
import junit.framework.TestResult;

import org.chromium.base.Log;
import org.chromium.base.test.util.SkipCheck;

import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

/**
 * A test result that can skip tests.
 */
public class BaseTestResult extends TestResult {
    private static final String TAG = "base_test";

    private static final int SLEEP_INTERVAL_MS = 50;
    private static final int WAIT_DURATION_MS = 5000;

    private final Instrumentation mInstrumentation;
    private final List<SkipCheck> mSkipChecks;
    private final List<PreTestHook> mPreTestHooks;

    /**
     * Creates an instance of BaseTestResult.
     */
    public BaseTestResult(Instrumentation instrumentation) {
        mSkipChecks = new ArrayList<>();
        mPreTestHooks = new ArrayList<>();
        mInstrumentation = instrumentation;
    }

    /**
     * An interface for classes that have some code to run before a test. They run after
     * {@link SkipCheck}s. Provides access to the test method (and the annotations defined for it)
     * and the instrumentation context.
     */
    public interface PreTestHook {
        /**
         * @param targetContext the instrumentation context that will be used during the test.
         * @param testMethod the test method to be run.
         */
        public void run(Context targetContext, Method testMethod);
    }

    /**
     * Adds a check for whether a test should run.
     *
     * @param skipCheck The check to add.
     */
    public void addSkipCheck(SkipCheck skipCheck) {
        mSkipChecks.add(skipCheck);
    }

    /**
     * Adds hooks that will be executed before each test that runs.
     *
     * @param preTestHook The hook to add.
     */
    public void addPreTestHook(PreTestHook preTestHook) {
        mPreTestHooks.add(preTestHook);
    }

    protected boolean shouldSkip(TestCase test) {
        for (SkipCheck s : mSkipChecks) {
            if (s.shouldSkip(test)) return true;
        }
        return false;
    }

    private void runPreTestHooks(TestCase test) {
        try {
            Method testMethod = test.getClass().getMethod(test.getName());
            Context targetContext = getTargetContext();

            for (PreTestHook hook : mPreTestHooks) {
                hook.run(targetContext, testMethod);
            }
        } catch (NoSuchMethodException e) {
            Log.e(TAG, "Unable to run pre test hooks.", e);
        }
    }

    @Override
    protected void run(TestCase test) {
        runPreTestHooks(test);

        if (shouldSkip(test)) {
            startTest(test);

            Bundle skipResult = new Bundle();
            skipResult.putString("class", test.getClass().getName());
            skipResult.putString("test", test.getName());
            skipResult.putBoolean("test_skipped", true);
            mInstrumentation.sendStatus(0, skipResult);

            endTest(test);
        } else {
            super.run(test);
        }
    }

    /**
     * Gets the target context.
     *
     * On older versions of Android, getTargetContext() may initially return null, so we have to
     * wait for it to become available.
     *
     * @return The target {@link Context} if available; null otherwise.
     */
    public Context getTargetContext() {
        Context targetContext = mInstrumentation.getTargetContext();
        try {
            long startTime = SystemClock.uptimeMillis();
            // TODO(jbudorick): Convert this to CriteriaHelper once that moves to base/.
            while (targetContext == null
                    && SystemClock.uptimeMillis() - startTime < WAIT_DURATION_MS) {
                Thread.sleep(SLEEP_INTERVAL_MS);
                targetContext = mInstrumentation.getTargetContext();
            }
        } catch (InterruptedException e) {
            Log.e(TAG, "Interrupted while attempting to initialize the command line.");
        }
        return targetContext;
    }
}
