// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.test;

import android.support.test.InstrumentationRegistry;
import android.support.test.uiautomator.UiDevice;

import org.junit.runners.model.Statement;

import org.chromium.base.Log;

import java.io.File;

/**
 * Statement that captures screenshots if |base| statement fails.
 *
 * If --screenshot-path commandline flag is given, this |Statement|
 * will save a screenshot to the specified path in the case of a test failure.
 */
public class ScreenshotOnFailureStatement extends Statement {
    private static final String TAG = "ScreenshotOnFail";

    private static final String EXTRA_SCREENSHOT_FILE =
            "org.chromium.base.test.ScreenshotOnFailureStatement.ScreenshotFile";

    private final Statement mBase;

    public ScreenshotOnFailureStatement(final Statement base) {
        mBase = base;
    }

    @Override
    public void evaluate() throws Throwable {
        try {
            mBase.evaluate();
        } catch (Throwable e) {
            takeScreenshot();
            throw e;
        }
    }

    private void takeScreenshot() {
        String screenshotFilePath =
                InstrumentationRegistry.getArguments().getString(EXTRA_SCREENSHOT_FILE);
        if (screenshotFilePath == null) {
            Log.d(TAG,
                    String.format("Did not save screenshot of failure. Must specify %s "
                                    + "instrumentation argument to enable this feature.",
                            EXTRA_SCREENSHOT_FILE));
            return;
        }

        UiDevice uiDevice = null;
        try {
            uiDevice = UiDevice.getInstance(InstrumentationRegistry.getInstrumentation());
        } catch (RuntimeException ex) {
            Log.d(TAG, "Failed to initialize UiDevice", ex);
            return;
        }

        File screenshotFile = new File(screenshotFilePath);
        File screenshotDir = screenshotFile.getParentFile();
        if (screenshotDir == null) {
            Log.d(TAG,
                    String.format(
                            "Failed to create parent directory for %s. Can't save screenshot.",
                            screenshotFile));
            return;
        }
        if (!screenshotDir.exists()) {
            if (!screenshotDir.mkdirs()) {
                Log.d(TAG,
                        String.format(
                                "Failed to create %s. Can't save screenshot.", screenshotDir));
                return;
            }
        }
        Log.d(TAG, String.format("Saving screenshot of test failure, %s", screenshotFile));
        uiDevice.takeScreenshot(screenshotFile);
    }
}
