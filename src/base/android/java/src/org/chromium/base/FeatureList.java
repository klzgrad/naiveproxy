// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import androidx.annotation.Nullable;
import androidx.annotation.VisibleForTesting;

import org.chromium.base.annotations.JNINamespace;
import org.chromium.base.annotations.MainDex;
import org.chromium.base.annotations.NativeMethods;
import org.chromium.base.library_loader.LibraryLoader;

import java.util.Map;

/**
 * Provides shared capabilities for feature flag support.
 */
@JNINamespace("base::android")
@MainDex
public class FeatureList {
    /** Map that stores substitution feature flags for tests. */
    private static @Nullable Map<String, Boolean> sTestFeatures;

    /** Access to default values of the native feature flag. */
    private static boolean sTestCanUseDefaults;

    private FeatureList() {}

    /**
     * @return Whether the native FeatureList has been initialized. If this method returns false,
     *         none of the methods in this class that require native access should be called (except
     *         in tests if test features have been set).
     */
    public static boolean isInitialized() {
        return hasTestFeatures() || isNativeInitialized();
    }

    /**
     * @return Whether the native FeatureList is initialized or not.
     */
    public static boolean isNativeInitialized() {
        if (!LibraryLoader.getInstance().isInitialized()) return false;
        // Even if the native library is loaded, the C++ FeatureList might not be initialized yet.
        // In that case, accessing it will not immediately fail, but instead cause a crash later
        // when it is initialized. Return whether the native FeatureList has been initialized,
        // so the return value can be tested, or asserted for a more actionable stack trace
        // on failure.
        //
        // The FeatureList is however guaranteed to be initialized by the time
        // AsyncInitializationActivity#finishNativeInitialization is called.
        return FeatureListJni.get().isInitialized();
    }

    /**
     * This is called explicitly for instrumentation tests via Features#applyForInstrumentation().
     * Unit tests and Robolectric tests must not invoke this and should rely on the {@link Features}
     * annotations to enable or disable any feature flags.
     */
    @VisibleForTesting
    public static void setTestCanUseDefaultsForTesting() {
        sTestCanUseDefaults = true;
    }

    /**
     * We reset the value to false after the instrumentation test to avoid any unwanted
     * persistence of the state. This is invoked by Features#reset().
     */
    @VisibleForTesting
    public static void resetTestCanUseDefaultsForTesting() {
        sTestCanUseDefaults = false;
    }

    /**
     * Sets the feature flags to use in JUnit tests, since native calls are not available there.
     */
    @VisibleForTesting
    public static void setTestFeatures(Map<String, Boolean> testFeatures) {
        sTestFeatures = testFeatures;
    }

    /**
     * @return Whether test feature values have been configured.
     */
    @VisibleForTesting
    public static boolean hasTestFeatures() {
        return sTestFeatures != null;
    }

    /**
     * Returns the test value of the feature with the given name.
     *
     * @param featureName The name of the feature to query.
     * @return The test value set for the feature, or null if no test value has been set.
     * @throws IllegalArgumentException if no test value was set and default values aren't allowed.
     */
    @VisibleForTesting
    public static Boolean getTestValueForFeature(String featureName) {
        if (hasTestFeatures()) {
            if (sTestFeatures.containsKey(featureName)) {
                return sTestFeatures.get(featureName);
            }
            if (!sTestCanUseDefaults) {
                throw new IllegalArgumentException("No test value configured for " + featureName);
            }
        }
        return null;
    }

    @NativeMethods
    interface Natives {
        boolean isInitialized();
    }
}
