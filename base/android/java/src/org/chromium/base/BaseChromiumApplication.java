// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import android.app.Application;
import android.content.Context;

import org.chromium.base.multidex.ChromiumMultiDexInstaller;

/**
 * Basic application functionality that should be shared among all browser applications.
 */
public class BaseChromiumApplication extends Application {
    private static final String TAG = "base";

    @Override
    protected void attachBaseContext(Context base) {
        super.attachBaseContext(base);
        assert getBaseContext() != null;
        checkAppBeingReplaced();
        if (BuildConfig.isMultidexEnabled()) {
            ChromiumMultiDexInstaller.install(this);
        }
    }

    /** Initializes the {@link CommandLine}. */
    public void initCommandLine() {}

    /**
     * This must only be called for contexts whose application is a subclass of
     * {@link BaseChromiumApplication}.
     */
    @VisibleForTesting
    public static void initCommandLine(Context context) {
        ((BaseChromiumApplication) context.getApplicationContext()).initCommandLine();
    }

    /** Ensure this application object is not out-of-date. */
    private void checkAppBeingReplaced() {
        // During app update the old apk can still be triggered by broadcasts and spin up an
        // out-of-date application. Kill old applications in this bad state. See
        // http://crbug.com/658130 for more context and http://b.android.com/56296 for the bug.
        if (getResources() == null) {
            Log.e(TAG, "getResources() null, closing app.");
            System.exit(0);
        }
    }

}
