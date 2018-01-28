// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import android.annotation.TargetApi;
import android.os.Build;
import android.security.NetworkSecurityPolicy;

import org.chromium.base.annotations.CalledByNative;

import java.lang.reflect.Method;

/**
 * Utility functions for testing features implemented in AndroidNetworkLibrary.
 */
public class AndroidNetworkLibraryTestUtil {
    /**
     * Helper for tests that simulates an app disallowing cleartext traffic entirely on M and newer.
     */
    @TargetApi(Build.VERSION_CODES.M)
    @CalledByNative
    private static void setUpSecurityPolicyForTesting(boolean cleartextPermitted) throws Exception {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            Method setCleartextTrafficPermitted = NetworkSecurityPolicy.class.getDeclaredMethod(
                    "setCleartextTrafficPermitted", boolean.class);
            setCleartextTrafficPermitted.invoke(
                    NetworkSecurityPolicy.getInstance(), cleartextPermitted);
        }
    }
}