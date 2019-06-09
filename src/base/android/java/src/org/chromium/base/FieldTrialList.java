// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.base;

import org.chromium.base.annotations.MainDex;

/**
 * Helper to get field trial information.
 */
@MainDex
public class FieldTrialList {

    private FieldTrialList() {}

    /**
     * @param trialName The name of the trial to get the group for.
     * @return The group name chosen for the named trial, or the empty string if the trial does
     *         not exist.
     */
    public static String findFullName(String trialName) {
        return nativeFindFullName(trialName);
    }

    /**
     * @param trialName The name of the trial to get the group for.
     * @return Whether the trial exists or not.
     */
    public static boolean trialExists(String trialName) {
        return nativeTrialExists(trialName);
    }

    /**
     * @param trialName    The name of the trial with the parameter.
     * @param parameterKey The key of the parameter.
     * @return The value of the parameter or an empty string if not found.
     */
    public static String getVariationParameter(String trialName, String parameterKey) {
        return nativeGetVariationParameter(trialName, parameterKey);
    }

    /**
     * Print active trials and their group assignments to logcat, for debugging purposes. Continue
     * prtinting new trials as they become active. This should be called at most once.
     */
    public static void logActiveTrials() {
        nativeLogActiveTrials();
    }

    private static native String nativeFindFullName(String trialName);
    private static native boolean nativeTrialExists(String trialName);
    private static native String nativeGetVariationParameter(String trialName, String parameterKey);
    private static native void nativeLogActiveTrials();
}
