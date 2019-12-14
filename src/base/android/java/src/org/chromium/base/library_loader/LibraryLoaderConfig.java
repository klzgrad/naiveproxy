// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base.library_loader;

/**
 * Build-time configuration of LibraryLoader.
 * These are in a separate class from LibraryLoader to ensure that they are inlined.
 */
// TODO(agrieve): Add @CheckDiscard once trichrome no longer requires a -keep for NativeLibraries:
//     https://cs.chromium.org/chromium/src/chrome/android/java/trichrome.flags?rcl=233c4879107285b519cb55b3e9a7f439b15a418f
public class LibraryLoaderConfig {
    private LibraryLoaderConfig() {}

    /**
     * Check that native library linker tests are enabled.
     * If not enabled, calls to testing functions will fail with an assertion
     * error.
     *
     * @return true if native library linker tests are enabled.
     */
    public static boolean areTestsEnabled() {
        return NativeLibraries.sEnableLinkerTests;
    }

    /**
     * Call this method to determine if this chromium project must
     * use this linker. If not, System.loadLibrary() should be used to load
     * libraries instead.
     */
    public static boolean useChromiumLinker() {
        return NativeLibraries.sUseLinker;
    }
}
