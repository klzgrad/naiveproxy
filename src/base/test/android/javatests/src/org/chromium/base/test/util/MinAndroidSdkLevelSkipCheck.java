// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test.util;

import android.os.Build;

import org.junit.runners.model.FrameworkMethod;

import org.chromium.base.Log;

/**
 * Checks the device's SDK level against any specified minimum requirement.
 */
public class MinAndroidSdkLevelSkipCheck extends SkipCheck {

    private static final String TAG = "base_test";

    /**
     * If {@link MinAndroidSdkLevel} is present, checks its value
     * against the device's SDK level.
     *
     * @param testCase The test to check.
     * @return true if the device's SDK level is below the specified minimum.
     */
    @Override
    public boolean shouldSkip(FrameworkMethod frameworkMethod) {
        int minSdkLevel = 0;
        for (MinAndroidSdkLevel m : AnnotationProcessingUtils.getAnnotations(
                     frameworkMethod.getMethod(), MinAndroidSdkLevel.class)) {
            minSdkLevel = Math.max(minSdkLevel, m.value());
        }
        if (Build.VERSION.SDK_INT < minSdkLevel) {
            Log.i(TAG, "Test " + frameworkMethod.getDeclaringClass().getName() + "#"
                    + frameworkMethod.getName() + " is not enabled at SDK level "
                    + Build.VERSION.SDK_INT + ".");
            return true;
        }
        return false;
    }

}
