// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import dalvik.system.BaseDexClassLoader;

import org.chromium.base.annotations.CalledByNative;

/** Utils to help working with android app bundles. */
public class BundleUtils {
    /* Returns true if the current build is a bundle. */
    @CalledByNative
    public static boolean isBundle() {
        return BuildConfig.IS_BUNDLE;
    }

    /* Returns absolute path to a native library in a feature module. */
    @CalledByNative
    private static String getNativeLibraryPath(String libraryName) {
        try (StrictModeContext ignored = StrictModeContext.allowDiskReads()) {
            return ((BaseDexClassLoader) ContextUtils.getApplicationContext().getClassLoader())
                    .findLibrary(libraryName);
        }
    }
}
